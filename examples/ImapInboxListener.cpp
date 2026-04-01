#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <expected>
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
    using usub::unet::mail::imap::BodyBasicPart;
    using usub::unet::mail::imap::BodyMultipart;
    using usub::unet::mail::imap::BodyStructure;
    using usub::unet::mail::imap::BodyTextPart;
    using usub::unet::mail::imap::Envelope;
    using usub::unet::mail::imap::HeaderFields;
    using usub::unet::mail::imap::MessageDataItem;
    using usub::unet::mail::imap::MessageDataList;
    using usub::unet::mail::imap::MessageDataValue;
    using usub::unet::mail::imap::NAddressList;
    using usub::unet::mail::imap::NString;
    using usub::unet::mail::imap::SequenceSetItem;
    using usub::unet::mail::imap::SequenceSetValue;
    using usub::unet::mail::imap::command::Fetch;
    using usub::unet::mail::imap::command::Login;
    using usub::unet::mail::imap::command::Select;
    using usub::unet::mail::imap::response::Exists;
    using FetchResponse = usub::unet::mail::imap::response::Fetch;
    using usub::unet::mail::imap::response::FetchedMessage;
    using usub::unet::mail::imap::response::ServerResponse;
    using usub::unet::mail::imap::response::UntaggedServerData;

    struct ListenerConfig {
        NetworkEndpoint endpoint{};
        std::string username{};
        std::string password{};
        std::string mailbox{"INBOX"};
        std::chrono::milliseconds reconnect_delay{std::chrono::seconds{2}};
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

    [[nodiscard]] std::string formatClientError(const ClientError &error) {
        std::string message = error.message;
        if (error.core_error.has_value()) {
            message.append(" | core=");
            message.append(error.core_error->message);
        }
        return message;
    }

    [[nodiscard]] std::optional<std::uint32_t> extractExists(const ServerResponse &response) {
        const auto *untagged = std::get_if<UntaggedServerData>(&response);
        if (untagged == nullptr) { return std::nullopt; }
        const auto *exists = std::get_if<Exists>(&untagged->data);
        if (exists == nullptr) { return std::nullopt; }
        return exists->count;
    }

    [[nodiscard]] std::optional<std::uint32_t> extractMaxExists(const std::vector<ServerResponse> &responses) {
        std::optional<std::uint32_t> max_exists{};
        for (const auto &response: responses) {
            const auto exists = extractExists(response);
            if (!exists.has_value()) { continue; }
            if (!max_exists.has_value() || *exists > *max_exists) { max_exists = *exists; }
        }
        return max_exists;
    }

    void printIndent(std::size_t level) {
        for (std::size_t i = 0; i < level; ++i) { std::cout << "  "; }
    }

    void printMultiline(std::string_view value, std::size_t indent) {
        std::size_t cursor = 0;
        while (cursor <= value.size()) {
            const auto line_end = value.find('\n', cursor);
            const auto line = value.substr(cursor, line_end == std::string_view::npos ? value.size() - cursor
                                                                                       : line_end - cursor);
            printIndent(indent);
            std::cout << line << '\n';
            if (line_end == std::string_view::npos) { break; }
            cursor = line_end + 1;
        }
    }

    void printNString(const NString &value) {
        if (std::holds_alternative<usub::unet::mail::imap::Nil>(value)) {
            std::cout << "NIL";
        } else {
            std::cout << '"' << std::get<std::string>(value) << '"';
        }
    }

    void printAddressList(const NAddressList &addresses, std::size_t indent) {
        if (std::holds_alternative<usub::unet::mail::imap::Nil>(addresses)) {
            printIndent(indent);
            std::cout << "NIL\n";
            return;
        }

        const auto &values = std::get<usub::unet::mail::imap::AddressList>(addresses);
        printIndent(indent);
        std::cout << "addresses[" << values.size() << "]\n";
        for (std::size_t i = 0; i < values.size(); ++i) {
            const auto &address = values[i];
            printIndent(indent + 1);
            std::cout << '[' << i << "]\n";
            printIndent(indent + 2);
            std::cout << "name: ";
            printNString(address.name);
            std::cout << '\n';
            printIndent(indent + 2);
            std::cout << "route: ";
            printNString(address.route);
            std::cout << '\n';
            printIndent(indent + 2);
            std::cout << "mailbox: ";
            printNString(address.mailbox);
            std::cout << '\n';
            printIndent(indent + 2);
            std::cout << "host: ";
            printNString(address.host);
            std::cout << '\n';
        }
    }

    void printEnvelope(const Envelope &envelope, std::size_t indent) {
        printIndent(indent);
        std::cout << "date: ";
        printNString(envelope.date);
        std::cout << '\n';
        printIndent(indent);
        std::cout << "subject: ";
        printNString(envelope.subject);
        std::cout << '\n';

        printIndent(indent);
        std::cout << "from:\n";
        printAddressList(envelope.from, indent + 1);
        printIndent(indent);
        std::cout << "sender:\n";
        printAddressList(envelope.sender, indent + 1);
        printIndent(indent);
        std::cout << "reply_to:\n";
        printAddressList(envelope.reply_to, indent + 1);
        printIndent(indent);
        std::cout << "to:\n";
        printAddressList(envelope.to, indent + 1);
        printIndent(indent);
        std::cout << "cc:\n";
        printAddressList(envelope.cc, indent + 1);
        printIndent(indent);
        std::cout << "bcc:\n";
        printAddressList(envelope.bcc, indent + 1);

        printIndent(indent);
        std::cout << "in_reply_to: ";
        printNString(envelope.in_reply_to);
        std::cout << '\n';
        printIndent(indent);
        std::cout << "message_id: ";
        printNString(envelope.message_id);
        std::cout << '\n';
    }

    void printBodyStructure(const BodyStructure &body, std::size_t indent);

    void printBodyPartFields(const BodyBasicPart &part, std::size_t indent) {
        printIndent(indent);
        std::cout << "media_type: " << part.fields.media_type << '/' << part.fields.media_subtype << '\n';
        if (!part.fields.parameters.empty()) {
            printIndent(indent);
            std::cout << "parameters:\n";
            for (const auto &[name, value]: part.fields.parameters) {
                printIndent(indent + 1);
                std::cout << name << ": ";
                printNString(value);
                std::cout << '\n';
            }
        }
        printIndent(indent);
        std::cout << "id: ";
        printNString(part.fields.id);
        std::cout << '\n';
        printIndent(indent);
        std::cout << "description: ";
        printNString(part.fields.description);
        std::cout << '\n';
        printIndent(indent);
        std::cout << "transfer_encoding: " << part.fields.transfer_encoding << '\n';
        if (part.fields.octet_count.has_value()) {
            printIndent(indent);
            std::cout << "octet_count: " << *part.fields.octet_count << '\n';
        }
    }

    void printBodyStructure(const BodyStructure &body, std::size_t indent) {
        if (const auto *multipart = body.asMultipart(); multipart != nullptr) {
            printIndent(indent);
            std::cout << "multipart/" << multipart->media_subtype << '\n';
            if (!multipart->parameters.empty()) {
                printIndent(indent);
                std::cout << "parameters:\n";
                for (const auto &[name, value]: multipart->parameters) {
                    printIndent(indent + 1);
                    std::cout << name << ": ";
                    printNString(value);
                    std::cout << '\n';
                }
            }
            for (std::size_t i = 0; i < multipart->parts.size(); ++i) {
                printIndent(indent);
                std::cout << "part[" << i << "]:\n";
                printBodyStructure(multipart->parts[i], indent + 1);
            }
            return;
        }

        if (const auto *text = body.asText(); text != nullptr) {
            printIndent(indent);
            std::cout << "text-part\n";
            printBodyPartFields(BodyBasicPart{.fields = text->fields}, indent + 1);
            if (text->line_count.has_value()) {
                printIndent(indent + 1);
                std::cout << "line_count: " << *text->line_count << '\n';
            }
            return;
        }

        if (const auto *basic = body.asBasic(); basic != nullptr) {
            printIndent(indent);
            std::cout << "basic-part\n";
            printBodyPartFields(*basic, indent + 1);
            return;
        }

        if (const auto *list = body.asGenericList(); list != nullptr) {
            printIndent(indent);
            std::cout << "generic-body-list[" << list->size() << "]\n";
        }
    }

    void printMessageDataValue(const MessageDataValue &value, std::size_t indent) {
        if (value.isNil()) {
            printIndent(indent);
            std::cout << "NIL\n";
            return;
        }
        if (const auto *number = value.asNumber(); number != nullptr) {
            printIndent(indent);
            std::cout << *number << '\n';
            return;
        }
        if (const auto *headers = value.asHeaders(); headers != nullptr) {
            printIndent(indent);
            std::cout << "headers[" << headers->values.size() << "]\n";
            for (const auto &[name, field_value]: headers->values) {
                printIndent(indent + 1);
                std::cout << name << ": " << field_value << '\n';
            }
            return;
        }
        if (const auto *body = value.asBodyStructure(); body != nullptr) {
            printBodyStructure(*body, indent);
            return;
        }
        if (const auto *envelope = value.asEnvelope(); envelope != nullptr) {
            printEnvelope(*envelope, indent);
            return;
        }
        if (const auto *list = value.asList(); list != nullptr) {
            printIndent(indent);
            std::cout << "list[" << list->size() << "]\n";
            for (std::size_t i = 0; i < list->size(); ++i) {
                printIndent(indent + 1);
                std::cout << '[' << i << "]\n";
                printMessageDataValue((*list)[i], indent + 2);
            }
            return;
        }
        if (const auto *text = value.asString(); text != nullptr) {
            printIndent(indent);
            std::cout << "string(" << text->size() << " bytes)";
            if (text->find('\n') == std::string::npos && text->find('\r') == std::string::npos) {
                std::cout << ": " << *text << '\n';
                return;
            }
            std::cout << ":\n";
            printMultiline(*text, indent + 1);
        }
    }

    void printFetchedMessage(const FetchedMessage &message) {
        std::cout << "sequence_number: " << message.sequence_number << '\n';
        for (const auto &item: message.items) {
            std::cout << item.name << ":\n";
            printMessageDataValue(item.value, 1);
        }
    }

    usub::uvent::task::Awaitable<std::expected<FetchResponse, ClientError>>
    fetchMessage(ImapClient &client, std::uint32_t sequence_number) {
        Fetch fetch_request{};
        fetch_request.sequence_set.push_back(SequenceSetItem{SequenceSetValue{sequence_number}, std::nullopt});
        fetch_request.data_item_names = {"UID", "FLAGS", "INTERNALDATE", "RFC822.SIZE", "ENVELOPE", "BODYSTRUCTURE", "BODY.PEEK[]"};
        co_return co_await client.fetch(fetch_request);
    }

    usub::uvent::task::Awaitable<std::expected<std::uint32_t, ClientError>>
    connectAndSelectMailbox(ImapClient &client, const ListenerConfig &config) {
        if (config.endpoint.scheme == "imaps") {
            usub::unet::core::stream::OpenSSLStream::Config ssl{};
            ssl.mode = usub::unet::core::stream::OpenSSLStream::MODE::CLIENT;
            ssl.verify_peer = config.verify_peer;
            ssl.server_name = config.endpoint.host;
            client.stream<usub::unet::core::stream::OpenSSLStream>().setConfig(std::move(ssl));
        }

        ClientNetworkOptions options{};
        options.connect_timeout = std::chrono::seconds{6};

        auto connected = co_await client.connect(config.endpoint, options);
        if (!connected) { co_return std::unexpected(connected.error()); }

        auto login = co_await client.login(Login{.username = config.username, .password = config.password});
        if (!login) { co_return std::unexpected(login.error()); }

        auto select = co_await client.select(Select{.mailbox_name = config.mailbox});
        if (!select) { co_return std::unexpected(select.error()); }

        co_return select->exists.value_or(0);
    }

    usub::uvent::task::Awaitable<void> listen(ListenerConfig config) {
        for (;;) {
            ImapClient client{};
            auto initial_exists = co_await connectAndSelectMailbox(client, config);
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
                    auto fetched = co_await fetchMessage(client, sequence);
                    if (!fetched) {
                        std::cerr << "fetch failed for seq=" << sequence << ": "
                                  << formatClientError(fetched.error()) << '\n';
                        restart_session = true;
                        break;
                    }

                    std::cout << "=== EMAIL " << sequence << " BEGIN ===\n";
                    if (fetched->messages.empty()) {
                        std::cout << "no fetched messages returned\n";
                    } else {
                        for (std::size_t i = 0; i < fetched->messages.size(); ++i) {
                            std::cout << "message[" << i << "]\n";
                            printFetchedMessage(fetched->messages[i]);
                        }
                    }
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
