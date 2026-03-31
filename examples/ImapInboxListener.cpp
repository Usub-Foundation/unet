#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <uvent/Uvent.h>

#include "unet/core/streams/openssl.hpp"
#include "unet/core/streams/plaintext.hpp"
#include "unet/mail/imap/client.hpp"

namespace {
    using ImapClient = usub::unet::mail::imap::ClientImpl<usub::unet::core::stream::PlainText,
                                                          usub::unet::core::stream::OpenSSLStream>;

    using usub::unet::mail::imap::ClientError;
    using usub::unet::mail::imap::ClientNetworkOptions;
    using usub::unet::mail::imap::NetworkEndpoint;
    using usub::unet::mail::imap::core::Atom;
    using usub::unet::mail::imap::core::COMMAND;
    using usub::unet::mail::imap::core::ParenthesizedList;
    using usub::unet::mail::imap::core::Response;
    using usub::unet::mail::imap::core::SessionState;
    using usub::unet::mail::imap::core::String;
    using usub::unet::mail::imap::core::Value;
    using usub::unet::mail::imap::core::extractExists;
    using usub::unet::mail::imap::core::extractFetchLiteral;

    struct ListenerConfig {
        NetworkEndpoint endpoint{};
        std::string username{};
        std::string password{};
        std::string mailbox{"INBOX"};
        std::chrono::milliseconds reconnect_delay{std::chrono::seconds(2)};
        bool verify_peer{true};
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

    [[nodiscard]] Value atom(std::string_view value) { return Value{.data = Atom{.value = std::string(value)}}; }

    [[nodiscard]] Value list(ParenthesizedList values) { return Value{.data = std::move(values)}; }

    [[nodiscard]] std::string formatClientError(const ClientError &error) {
        std::string message = error.message;
        if (error.core_error.has_value()) {
            message.append(" | core=");
            message.append(error.core_error->message);
            message.append(" @");
            message.append(std::to_string(error.core_error->offset));
        }
        return message;
    }

    [[nodiscard]] std::optional<std::uint32_t> extractMaxExists(const std::vector<Response> &responses) {
        std::optional<std::uint32_t> max_exists{};
        for (const auto &response: responses) {
            const auto exists = extractExists(response);
            if (!exists.has_value()) { continue; }
            if (!max_exists.has_value() || *exists > *max_exists) { max_exists = *exists; }
        }
        return max_exists;
    }

    usub::uvent::task::Awaitable<std::expected<std::string, ClientError>>
    fetchMessageText(ImapClient &client, std::uint32_t sequence_number) {
        const std::string sequence = std::to_string(sequence_number);

        auto fetch = co_await client.command(COMMAND::FETCH, {atom(sequence), list({atom("BODY.PEEK[TEXT]")})});
        if (!fetch) { co_return std::unexpected(fetch.error()); }

        for (const auto &response: *fetch) {
            const auto text = extractFetchLiteral(response);
            if (text.has_value()) { co_return std::string(*text); }
        }

        auto fallback = co_await client.command(COMMAND::FETCH, {atom(sequence), list({atom("BODY.PEEK[]")})});
        if (!fallback) { co_return std::unexpected(fallback.error()); }

        for (const auto &response: *fallback) {
            const auto text = extractFetchLiteral(response);
            if (text.has_value()) { co_return std::string(*text); }
        }

        co_return std::unexpected(
                ClientError{.code = ClientError::CODE::PROTOCOL_STATE, .message = "FETCH completed without body text"});
    }

    usub::uvent::task::Awaitable<std::expected<std::uint32_t, ClientError>>
    connectAndSelectInbox(ImapClient &client, const ListenerConfig &config) {
        if (config.endpoint.scheme == "imaps") {
            usub::unet::core::stream::OpenSSLStream::Config ssl{};
            ssl.mode = usub::unet::core::stream::OpenSSLStream::MODE::CLIENT;
            ssl.verify_peer = config.verify_peer;
            ssl.server_name = config.endpoint.host;
            client.stream<usub::unet::core::stream::OpenSSLStream>().setConfig(std::move(ssl));
        }

        ClientNetworkOptions options{};
        options.connect_timeout = std::chrono::seconds(6);

        auto connected = co_await client.connect(config.endpoint, options);
        if (!connected) { co_return std::unexpected(connected.error()); }

        auto login = co_await client.login(config.username, config.password);
        if (!login) { co_return std::unexpected(login.error()); }

        if (client.session().state() != SessionState::Authenticated) {
            co_return std::unexpected(
                    ClientError{.code = ClientError::CODE::PROTOCOL_STATE, .message = "LOGIN did not authenticate"});
        }

        auto select = co_await client.command(COMMAND::SELECT, {atom(config.mailbox)});
        if (!select) { co_return std::unexpected(select.error()); }
        if (client.session().state() != SessionState::Selected) {
            co_return std::unexpected(
                    ClientError{.code = ClientError::CODE::PROTOCOL_STATE, .message = "SELECT did not open mailbox"});
        }

        co_return extractMaxExists(*select).value_or(0);
    }

    usub::uvent::task::Awaitable<void> listen(ListenerConfig config) {
        for (;;) {
            ImapClient client{};

            auto initial_exists = co_await connectAndSelectInbox(client, config);
            if (!initial_exists) {
                std::cerr << "connect/select failed: " << formatClientError(initial_exists.error()) << '\n';
                co_await client.close();
                co_await usub::uvent::system::this_coroutine::sleep_for(config.reconnect_delay);
                continue;
            }

            std::uint32_t known_exists = *initial_exists;
            std::cout << "listening on " << config.mailbox << " at " << config.endpoint.host
                      << " starting_exists=" << known_exists << '\n';

            bool restart_session = false;
            while (!restart_session) {
                auto idle_started = co_await client.idleStart();
                if (!idle_started) {
                    std::cerr << "idle start failed: " << formatClientError(idle_started.error()) << '\n';
                    restart_session = true;
                    break;
                }

                std::uint32_t announced_exists = extractMaxExists(*idle_started).value_or(known_exists);
                while (announced_exists <= known_exists) {
                    auto response = co_await client.readResponse();
                    if (!response) {
                        std::cerr << "idle read failed: " << formatClientError(response.error()) << '\n';
                        restart_session = true;
                        break;
                    }

                    if (const auto exists = extractExists(*response); exists.has_value() && *exists > announced_exists) {
                        announced_exists = *exists;
                    }
                }

                if (restart_session) { break; }

                auto idle_finished = co_await client.idleDone();
                if (!idle_finished) {
                    std::cerr << "idle done failed: " << formatClientError(idle_finished.error()) << '\n';
                    restart_session = true;
                    break;
                }

                if (const auto exists = extractMaxExists(*idle_finished); exists.has_value() && *exists > announced_exists) {
                    announced_exists = *exists;
                }

                if (announced_exists < known_exists) {
                    known_exists = announced_exists;
                    continue;
                }

                for (std::uint32_t sequence = known_exists + 1; sequence <= announced_exists; ++sequence) {
                    auto message_text = co_await fetchMessageText(client, sequence);
                    if (!message_text) {
                        std::cerr << "fetch text failed for seq=" << sequence << ": "
                                  << formatClientError(message_text.error()) << '\n';
                        restart_session = true;
                        break;
                    }

                    std::cout << "=== EMAIL " << sequence << " BEGIN ===\n";
                    std::cout << *message_text << '\n';
                    std::cout << "=== EMAIL " << sequence << " END ===\n";
                }

                known_exists = announced_exists;
            }

            co_await client.close();
            co_await usub::uvent::system::this_coroutine::sleep_for(config.reconnect_delay);
        }
    }

    std::optional<ListenerConfig> loadConfig() {
        const auto username = getEnv("UNET_IMAP_LISTENER_USER");
        const auto password = getEnv("UNET_IMAP_LISTENER_PASSWORD");
        if (!username.has_value() || !password.has_value()) {
            std::cerr << "set UNET_IMAP_LISTENER_USER and UNET_IMAP_LISTENER_PASSWORD\n";
            return std::nullopt;
        }

        ListenerConfig config{};
        config.endpoint.host = getEnv("UNET_IMAP_LISTENER_HOST").value_or("imap.gmail.com");
        config.endpoint.scheme = getEnv("UNET_IMAP_LISTENER_SCHEME").value_or("imaps");
        config.mailbox = getEnv("UNET_IMAP_LISTENER_MAILBOX").value_or("INBOX");
        config.username = *username;
        config.password = *password;
        config.verify_peer = !envEnabled("UNET_IMAP_LISTENER_INSECURE_TLS");

        if (const auto port = getEnv("UNET_IMAP_LISTENER_PORT"); port.has_value()) {
            config.endpoint.port = static_cast<std::uint16_t>(std::stoul(*port));
        }

        if (const auto delay = getEnv("UNET_IMAP_LISTENER_RECONNECT_MS"); delay.has_value()) {
            config.reconnect_delay = std::chrono::milliseconds{std::stoll(*delay)};
        }

        return config;
    }
}// namespace

int main() {
    const auto config = loadConfig();
    if (!config.has_value()) { return 1; }

    usub::Uvent runtime{2};
    usub::uvent::system::co_spawn(listen(*config));
    runtime.run();
    return 0;
}
