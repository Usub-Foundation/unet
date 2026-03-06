#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <uvent/Uvent.h>

#include "unet/core/streams/openssl.hpp"
#include "unet/core/streams/plaintext.hpp"
#include "unet/mail/imap/client.hpp"
#include "unet/mail/imap/core/capability.hpp"

namespace {
    using ImapClient = usub::unet::mail::imap::ClientImpl<usub::unet::core::stream::PlainText,
                                                          usub::unet::core::stream::OpenSSLStream>;

    using usub::unet::mail::imap::ClientNetworkOptions;
    using usub::unet::mail::imap::NetworkEndpoint;
    using usub::unet::mail::imap::core::Atom;
    using usub::unet::mail::imap::core::COMMAND;
    using usub::unet::mail::imap::core::ParenthesizedList;
    using usub::unet::mail::imap::core::parseCapabilities;
    using usub::unet::mail::imap::core::Response;
    using usub::unet::mail::imap::core::ResponseCondition;
    using usub::unet::mail::imap::core::SessionState;
    using usub::unet::mail::imap::core::String;
    using usub::unet::mail::imap::core::TaggedResponse;
    using usub::unet::mail::imap::core::UntaggedDataResponse;
    using usub::unet::mail::imap::core::UntaggedNumericResponse;
    using usub::unet::mail::imap::core::UntaggedResponse;
    using usub::unet::mail::imap::core::UntaggedStatusResponse;
    using usub::unet::mail::imap::core::Value;

    struct SharedState {
        std::atomic<bool> finished{false};
        std::mutex mutex{};
        std::string error{};
        std::vector<std::string> capabilities{};

        void fail(std::string message) {
            std::lock_guard<std::mutex> lock(mutex);
            if (error.empty()) { error = std::move(message); }
        }

        void done(std::vector<std::string> caps) {
            std::lock_guard<std::mutex> lock(mutex);
            capabilities = std::move(caps);
            finished.store(true, std::memory_order_release);
        }
    };

    [[nodiscard]] std::optional<std::string> getEnv(std::string_view name) {
        if (const char *value = std::getenv(std::string(name).c_str()); value != nullptr) { return std::string(value); }
        return std::nullopt;
    }

    [[nodiscard]] bool envEnabled(std::string_view name) {
        const auto value = getEnv(name);
        if (!value.has_value()) { return false; }

        std::string lowered = *value;
        for (char &ch: lowered) {
            if (ch >= 'A' && ch <= 'Z') { ch = static_cast<char>(ch - 'A' + 'a'); }
        }

        return lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on";
    }

    [[nodiscard]] std::string escapeForLog(std::string_view text, std::size_t max_bytes = 256) {
        std::string out;
        out.reserve(std::min(max_bytes, text.size()) + 16);

        const std::size_t limit = std::min(max_bytes, text.size());
        for (std::size_t i = 0; i < limit; ++i) {
            const char ch = text[i];
            switch (ch) {
                case '\r':
                    out.append("\\r");
                    break;
                case '\n':
                    out.append("\\n");
                    break;
                case '\t':
                    out.append("\\t");
                    break;
                default:
                    if (static_cast<unsigned char>(ch) < 0x20) {
                        out.push_back('?');
                    } else {
                        out.push_back(ch);
                    }
                    break;
            }
        }

        if (text.size() > limit) {
            out.append(" ...(+");
            out.append(std::to_string(text.size() - limit));
            out.append(" bytes)");
        }

        return out;
    }

    [[nodiscard]] std::string_view conditionToString(ResponseCondition condition) {
        switch (condition) {
            case ResponseCondition::OK:
                return "OK";
            case ResponseCondition::NO:
                return "NO";
            case ResponseCondition::BAD:
                return "BAD";
            case ResponseCondition::PREAUTH:
                return "PREAUTH";
            case ResponseCondition::BYE:
                return "BYE";
            case ResponseCondition::UNKNOWN:
                return "UNKNOWN";
        }
        return "UNKNOWN";
    }

    [[nodiscard]] Value atom(std::string_view value) { return Value{.data = Atom{.value = std::string(value)}}; }

    [[nodiscard]] Value quoted(std::string_view value) {
        return Value{.data = String{.form = String::Form::Quoted, .value = std::string(value)}};
    }

    [[nodiscard]] Value list(ParenthesizedList values) { return Value{.data = std::move(values)}; }

    void logResponse(std::size_t index, const Response &response) {
        std::cout << "  [" << index << "] ";

        if (response.kind == Response::Kind::Tagged) {
            const auto &tagged = std::get<TaggedResponse>(response.data);
            std::cout << "TAGGED tag=" << tagged.tag.value << " status=" << conditionToString(tagged.status.condition);
            if (tagged.status.code.has_value()) {
                std::cout << " code=[" << tagged.status.code->name.value;
                if (!tagged.status.code->text.empty()) { std::cout << ' ' << tagged.status.code->text; }
                std::cout << ']';
            }
            if (!tagged.status.text.empty()) { std::cout << " text=\"" << tagged.status.text << "\""; }
            std::cout << '\n';
        } else if (response.kind == Response::Kind::Continuation) {
            const auto &cont = std::get<usub::unet::mail::imap::core::ContinuationResponse>(response.data);
            std::cout << "CONTINUATION text=\"" << cont.text << "\"\n";
        } else {
            const auto &untagged = std::get<UntaggedResponse>(response.data);

            if (const auto *status = std::get_if<UntaggedStatusResponse>(&untagged.payload)) {
                std::cout << "UNTAGGED-STATUS status=" << conditionToString(status->status.condition);
                if (status->status.code.has_value()) {
                    std::cout << " code=[" << status->status.code->name.value;
                    if (!status->status.code->text.empty()) { std::cout << ' ' << status->status.code->text; }
                    std::cout << ']';
                }
                if (!status->status.text.empty()) { std::cout << " text=\"" << status->status.text << "\""; }
                std::cout << '\n';
            } else if (const auto *numeric = std::get_if<UntaggedNumericResponse>(&untagged.payload)) {
                std::cout << "UNTAGGED-NUMERIC number=" << numeric->number << " atom=" << numeric->atom.value;
                if (!numeric->text.empty()) { std::cout << " text=\"" << numeric->text << "\""; }
                std::cout << '\n';

                if (numeric->literal.has_value()) {
                    std::cout << "      literal form="
                              << (numeric->literal->form == String::Form::NonSynchronizingLiteral ? "non-sync"
                                  : numeric->literal->form == String::Form::SynchronizingLiteral  ? "sync"
                                                                                                  : "quoted")
                              << " bytes=" << numeric->literal->value.size() << '\n';
                    std::cout << "      literal-preview: " << escapeForLog(numeric->literal->value, 400) << '\n';
                }
            } else if (const auto *data = std::get_if<UntaggedDataResponse>(&untagged.payload)) {
                std::cout << "UNTAGGED-DATA atom=" << data->atom.value;
                if (!data->text.empty()) { std::cout << " text=\"" << data->text << "\""; }
                std::cout << '\n';

                if (data->literal.has_value()) {
                    std::cout << "      literal form="
                              << (data->literal->form == String::Form::NonSynchronizingLiteral ? "non-sync"
                                  : data->literal->form == String::Form::SynchronizingLiteral  ? "sync"
                                                                                               : "quoted")
                              << " bytes=" << data->literal->value.size() << '\n';
                    std::cout << "      literal-preview: " << escapeForLog(data->literal->value, 400) << '\n';
                }
            }
        }

        if (!response.raw.empty()) { std::cout << "      raw-preview: " << escapeForLog(response.raw, 20000) << '\n'; }
    }

    void logResponses(std::string_view title, const std::vector<Response> &responses) {
        std::cout << "\\n=== " << title << " (" << responses.size() << " responses) ===\n";
        for (std::size_t i = 0; i < responses.size(); ++i) { logResponse(i, responses[i]); }
    }

    [[nodiscard]] bool finalTaggedOk(const std::vector<Response> &responses) {
        for (auto it = responses.rbegin(); it != responses.rend(); ++it) {
            if (it->kind != Response::Kind::Tagged) { continue; }
            const auto &tagged = std::get<TaggedResponse>(it->data);
            return tagged.status.condition == ResponseCondition::OK;
        }
        return false;
    }

    [[nodiscard]] std::optional<std::uint32_t> extractExists(const std::vector<Response> &responses) {
        for (const auto &response: responses) {
            if (response.kind != Response::Kind::Untagged) { continue; }
            const auto &untagged = std::get<UntaggedResponse>(response.data);
            const auto *numeric = std::get_if<UntaggedNumericResponse>(&untagged.payload);
            if (!numeric) { continue; }
            if (numeric->atom.value == "EXISTS") { return numeric->number; }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::string formatClientError(const usub::unet::mail::imap::ClientError &error) {
        std::string message = error.message;
        if (error.core_error.has_value()) {
            message.append(" | core=");
            message.append(error.core_error->message);
            message.append(" @");
            message.append(std::to_string(error.core_error->offset));
        }
        return message;
    }

    usub::uvent::task::Awaitable<void> run_live_test(SharedState &state, NetworkEndpoint endpoint,
                                                     std::optional<std::string> username,
                                                     std::optional<std::string> password, bool verify_peer) {
        ImapClient client{};

        if (endpoint.scheme == "imaps") {
            usub::unet::core::stream::OpenSSLStream::Config cfg{};
            cfg.mode = usub::unet::core::stream::OpenSSLStream::MODE::CLIENT;
            cfg.verify_peer = verify_peer;
            cfg.server_name = endpoint.host;
            client.stream<usub::unet::core::stream::OpenSSLStream>().setConfig(std::move(cfg));
        }

        ClientNetworkOptions options{};
        options.connect_timeout = std::chrono::milliseconds{6000};

        auto connected = co_await client.connect(endpoint, options);
        if (!connected) {
            state.fail("imap connect failed: " + formatClientError(connected.error()));
            co_return;
        }

        if (client.greeting().has_value()) {
            std::cout << "\\n=== GREETING ===\n";
            logResponse(0, *client.greeting());
        }

        auto capability = co_await client.capability();
        if (!capability) {
            state.fail("CAPABILITY command failed: " + formatClientError(capability.error()));
            co_await client.close();
            co_return;
        }
        logResponses("CAPABILITY", *capability);

        if (!finalTaggedOk(*capability)) {
            state.fail("CAPABILITY command did not finish with tagged OK");
            co_await client.close();
            co_return;
        }

        std::optional<std::vector<std::string>> capabilities{};
        for (const auto &response: *capability) {
            auto parsed = parseCapabilities(response);
            if (parsed) {
                capabilities = parsed->values;
                break;
            }
        }

        if (!capabilities.has_value() || capabilities->empty()) {
            state.fail("could not parse CAPABILITY tokens from server response");
            co_await client.close();
            co_return;
        }

        if (username.has_value() && password.has_value()) {
            auto login = co_await client.login(*username, *password);
            if (!login) {
                state.fail("LOGIN command failed: " + formatClientError(login.error()));
                co_await client.close();
                co_return;
            }
            logResponses("LOGIN", *login);

            if (!finalTaggedOk(*login)) {
                state.fail("LOGIN command did not finish with tagged OK");
                co_await client.close();
                co_return;
            }
        }

        const auto state_now = client.session().state();
        const bool can_access_mailbox = state_now == SessionState::Authenticated || state_now == SessionState::Selected;

        if (!can_access_mailbox) {
            std::cout << "\\nSkipping mailbox retrieval: session is not authenticated (set user/password env vars).\n";
        } else {
            auto list_mailboxes = co_await client.command(COMMAND::LIST, {quoted(""), quoted("*")});
            if (!list_mailboxes) {
                state.fail("LIST command failed: " + formatClientError(list_mailboxes.error()));
                co_await client.close();
                co_return;
            }
            logResponses("LIST \"\" \"*\"", *list_mailboxes);

            auto select_inbox = co_await client.command(COMMAND::SELECT, {atom("INBOX")});
            if (!select_inbox) {
                state.fail("SELECT INBOX failed: " + formatClientError(select_inbox.error()));
                co_await client.close();
                co_return;
            }
            logResponses("SELECT INBOX", *select_inbox);

            if (!finalTaggedOk(*select_inbox)) {
                state.fail("SELECT INBOX did not finish with tagged OK");
                co_await client.close();
                co_return;
            }

            const auto exists = extractExists(*select_inbox).value_or(0);
            if (exists == 0) {
                std::cout << "\\nNo messages found in INBOX (EXISTS=0).\n";
            } else {
                const std::uint32_t sample_count = 5;
                const std::uint32_t start = exists > sample_count ? exists - sample_count + 1 : 1;
                const std::string sample_set = (start == exists)
                                                       ? std::to_string(exists)
                                                       : (std::to_string(start) + ":" + std::to_string(exists));

                auto fetch_meta = co_await client.command(
                        COMMAND::FETCH, {atom(sample_set), list({atom("UID"), atom("FLAGS"), atom("INTERNALDATE"),
                                                                 atom("RFC822.SIZE")})});
                if (!fetch_meta) {
                    state.fail("FETCH metadata failed: " + formatClientError(fetch_meta.error()));
                    co_await client.close();
                    co_return;
                }
                logResponses("FETCH " + sample_set + " (UID FLAGS INTERNALDATE RFC822.SIZE)", *fetch_meta);

                if (!finalTaggedOk(*fetch_meta)) {
                    state.fail("FETCH metadata did not finish with tagged OK");
                    co_await client.close();
                    co_return;
                }

                const std::string latest_seq = std::to_string(exists);

                auto fetch_header = co_await client.command(
                        COMMAND::FETCH, {atom(latest_seq), list({atom("UID"), atom("BODY.PEEK[HEADER]")})});
                if (!fetch_header) {
                    state.fail("FETCH header failed: " + formatClientError(fetch_header.error()));
                    co_await client.close();
                    co_return;
                }
                logResponses("FETCH " + latest_seq + " (UID BODY.PEEK[HEADER])", *fetch_header);

                if (!finalTaggedOk(*fetch_header)) {
                    state.fail("FETCH header did not finish with tagged OK");
                    co_await client.close();
                    co_return;
                }

                auto fetch_text = co_await client.command(
                        COMMAND::FETCH, {atom(latest_seq), list({atom("UID"), atom("BODY.PEEK[TEXT]<0.512>")})});
                if (!fetch_text) {
                    state.fail("FETCH text preview failed: " + formatClientError(fetch_text.error()));
                    co_await client.close();
                    co_return;
                }
                logResponses("FETCH " + latest_seq + " (UID BODY.PEEK[TEXT]<0.512>)", *fetch_text);

                if (!finalTaggedOk(*fetch_text)) {
                    state.fail("FETCH text preview did not finish with tagged OK");
                    co_await client.close();
                    co_return;
                }
            }
        }

        auto logout = co_await client.logout();
        if (!logout) {
            state.fail("LOGOUT command failed: " + formatClientError(logout.error()));
            co_await client.close();
            co_return;
        }
        logResponses("LOGOUT", *logout);

        if (!finalTaggedOk(*logout)) {
            state.fail("LOGOUT command did not finish with tagged OK");
            co_await client.close();
            co_return;
        }

        co_await client.close();
        state.done(std::move(*capabilities));
        co_return;
    }
}// namespace

int main() {
    if (!envEnabled("UNET_RUN_LIVE_IMAP")) {
        std::cout << "skipping live IMAP test (set UNET_RUN_LIVE_IMAP=1 to enable)\n";
        return 0;
    }

    NetworkEndpoint endpoint{};
    endpoint.host = getEnv("UNET_IMAP_TEST_HOST").value_or("imap.gmail.com");
    endpoint.scheme = getEnv("UNET_IMAP_TEST_SCHEME").value_or("imaps");

    if (const auto port = getEnv("UNET_IMAP_TEST_PORT"); port.has_value()) {
        endpoint.port = static_cast<std::uint16_t>(std::stoul(*port));
    }

    const auto username = getEnv("UNET_IMAP_TEST_USER");
    const auto password = getEnv("UNET_IMAP_TEST_PASSWORD");
    const bool verify_peer = !envEnabled("UNET_IMAP_INSECURE_TLS");

    SharedState state{};
    usub::Uvent uvent{2};
    usub::uvent::system::co_spawn(run_live_test(state, endpoint, username, password, verify_peer));

    std::jthread runner([&uvent]() { uvent.run(); });
    std::this_thread::sleep_for(std::chrono::seconds(30));
    uvent.stop();

    if (!state.error.empty()) {
        std::cerr << "live IMAP test failed: " << state.error << '\n';
        return 1;
    }

    if (!state.finished.load(std::memory_order_acquire)) {
        std::cerr << "live IMAP test failed: timeout waiting for completion\n";
        return 1;
    }

    std::cout << "live IMAP test passed. capabilities=";
    for (std::size_t i = 0; i < state.capabilities.size(); ++i) {
        if (i != 0) { std::cout << ','; }
        std::cout << state.capabilities[i];
    }
    std::cout << '\n';
    return 0;
}
