#pragma once

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <uvent/Uvent.h>

#include "unet/mail/imap/core/command.hpp"
#include "unet/mail/imap/core/error.hpp"
#include "unet/mail/imap/core/response.hpp"
#include "unet/mail/imap/wire/command_serializer.hpp"
#include "unet/mail/imap/wire/response_parser.hpp"

namespace usub::unet::mail::imap {

    struct ClientOptions {
        std::string tag_prefix{"A"};
        std::uint32_t initial_tag_counter{1};
        std::uint32_t max_tag_counter{9999};
    };

    struct NetworkEndpoint {
        std::string host{};
        std::string scheme{"imaps"};
        std::uint16_t port{0};
    };

    struct ClientNetworkOptions {
        std::chrono::milliseconds connect_timeout{std::chrono::milliseconds{4000}};
        bool read_greeting{true};
    };

    struct ClientError {
        enum class CODE : std::uint8_t {
            INVALID_INPUT,
            STREAM_UNAVAILABLE,
            CONNECT_FAILED,
            WRITE_FAILED,
            READ_FAILED,
            PARSE_FAILED,
            PROTOCOL_STATE,
            CLOSE_FAILED,
            UNSUPPORTED,
        };

        CODE code{CODE::INVALID_INPUT};
        std::string message{};
        std::optional<Error> core_error{};
    };

    template<typename... Streams>
    class ClientImpl {
    public:
        enum class STATUS : std::uint8_t {
            DISCONNECTED,
            NOT_AUTHENTICATED,
            AUTHENTICATED,
            SELECTED,
            LOGOUT,
        };

        template<typename T, typename = void>
        struct has_ssl_tag : std::false_type {};

        template<typename T>
        struct has_ssl_tag<T, std::void_t<decltype(T::ssl)>> : std::bool_constant<static_cast<bool>(T::ssl)> {};

        ClientImpl() = default;
        explicit ClientImpl(ClientOptions options) : options_(std::move(options)), tag_counter_(options_.initial_tag_counter) {}
        ClientImpl(ClientOptions options, Streams... streams)
            : streams_(std::move(streams)...), options_(std::move(options)), tag_counter_(options_.initial_tag_counter) {}
        explicit ClientImpl(Streams... streams) : streams_(std::move(streams)...) {}

        [[nodiscard]] STATUS status() const noexcept { return status_; }
        [[nodiscard]] bool connected() const noexcept { return connected_; }
        [[nodiscard]] bool idling() const noexcept { return idle_active_; }
        [[nodiscard]] const std::vector<std::string> &capabilities() const noexcept { return capabilities_; }
        [[nodiscard]] const std::optional<response::ServerResponse> &greeting() const noexcept { return greeting_; }

        template<typename Stream>
        Stream &stream() {
            static_assert((std::is_same_v<Stream, Streams> || ...),
                          "Requested stream is not part of this ClientImpl specialization");
            return std::get<Stream>(streams_);
        }

        [[nodiscard]] bool hasCapability(std::string_view name) const noexcept {
            return std::any_of(capabilities_.begin(), capabilities_.end(), [&](const std::string &value) {
                return asciiEqual(value, name);
            });
        }

        usub::uvent::task::Awaitable<std::expected<void, ClientError>>
        connect(NetworkEndpoint endpoint, const ClientNetworkOptions &options = ClientNetworkOptions{}) {
            if (endpoint.host.empty()) {
                co_return std::unexpected(
                        ClientError{.code = ClientError::CODE::INVALID_INPUT, .message = "imap endpoint host must not be empty"});
            }

            std::string scheme = endpoint.scheme;
            std::transform(scheme.begin(), scheme.end(), scheme.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            if (scheme.empty()) { scheme = "imaps"; }

            if (scheme == "imaps") {
                use_ssl_ = true;
                if constexpr (std::is_void_v<typename first_stream_by_ssl<true, Streams...>::type>) {
                    co_return std::unexpected(
                            ClientError{.code = ClientError::CODE::STREAM_UNAVAILABLE,
                                        .message = "IMAPS requested but no TLS stream is configured"});
                }
                if (endpoint.port == 0) { endpoint.port = 993; }
            } else if (scheme == "imap") {
                use_ssl_ = false;
                if constexpr (std::is_void_v<typename first_stream_by_ssl<false, Streams...>::type>) {
                    co_return std::unexpected(
                            ClientError{.code = ClientError::CODE::STREAM_UNAVAILABLE,
                                        .message = "IMAP requested but no plaintext stream is configured"});
                }
                if (endpoint.port == 0) { endpoint.port = 143; }
            } else {
                co_return std::unexpected(ClientError{.code = ClientError::CODE::INVALID_INPUT,
                                                      .message = "unsupported IMAP scheme (expected imap/imaps)"});
            }

            co_await close();
            resetProtocolState();

            socket_.emplace();
            std::string connect_host = endpoint.host;
            std::string connect_port = std::to_string(endpoint.port);
            auto connect_error = co_await socket_->async_connect(connect_host, connect_port, options.connect_timeout);
            if (connect_error.has_value()) {
                socket_.reset();
                co_return std::unexpected(ClientError{.code = ClientError::CODE::CONNECT_FAILED,
                                                      .message = "IMAP connect failed"});
            }

            connected_ = true;

            if (options.read_greeting) {
                auto greeting = co_await readResponse();
                if (!greeting) {
                    co_await close();
                    co_return std::unexpected(greeting.error());
                }
                greeting_ = *greeting;
                applyServerResponse(*greeting);
            }

            co_return {};
        }

        usub::uvent::task::Awaitable<std::expected<response::ServerResponse, ClientError>> readResponse() {
            if (!connected_ || !socket_.has_value()) {
                co_return std::unexpected(
                        ClientError{.code = ClientError::CODE::INVALID_INPUT, .message = "client is not connected"});
            }

            while (true) {
                auto parsed = parser_.next();
                if (!parsed) {
                    co_return std::unexpected(ClientError{.code = ClientError::CODE::PARSE_FAILED,
                                                          .message = "failed to parse IMAP response",
                                                          .core_error = parsed.error()});
                }
                if (parsed->has_value()) { co_return std::move(**parsed); }

                auto read = co_await readRaw();
                if (!read) { co_return std::unexpected(read.error()); }
                if (*read <= 0) {
                    connected_ = false;
                    status_ = STATUS::DISCONNECTED;
                    co_return std::unexpected(ClientError{.code = ClientError::CODE::READ_FAILED,
                                                          .message = "connection closed while waiting for IMAP response"});
                }

                std::string_view chunk{reinterpret_cast<const char *>(read_buffer_.data()),
                                       static_cast<std::size_t>(*read)};
                auto fed = parser_.feed(chunk);
                if (!fed) {
                    co_return std::unexpected(ClientError{.code = ClientError::CODE::PARSE_FAILED,
                                                          .message = "failed feeding bytes to IMAP parser",
                                                          .core_error = fed.error()});
                }
            }
        }

        template<typename CommandData>
        usub::uvent::task::Awaitable<std::expected<std::vector<response::ServerResponse>, ClientError>>
        command(CommandData data) {
            auto tag = nextTag();
            if (!tag) {
                co_return std::unexpected(ClientError{.code = ClientError::CODE::PROTOCOL_STATE,
                                                      .message = "failed to allocate IMAP tag",
                                                      .core_error = tag.error()});
            }

            co_return co_await command(command::Command<CommandData>{.tag = *tag, .data = std::move(data)});
        }

        template<typename CommandData>
        usub::uvent::task::Awaitable<std::expected<std::vector<response::ServerResponse>, ClientError>>
        command(const command::Command<CommandData> &request) {
            if (!connected_) {
                co_return std::unexpected(
                        ClientError{.code = ClientError::CODE::INVALID_INPUT, .message = "client is not connected"});
            }
            if (idle_active_) {
                co_return std::unexpected(ClientError{.code = ClientError::CODE::PROTOCOL_STATE,
                                                      .message = "cannot send command while IDLE is active"});
            }
            if constexpr (std::is_same_v<CommandData, command::Idle>) {
                co_return std::unexpected(ClientError{.code = ClientError::CODE::UNSUPPORTED,
                                                      .message = "use idleStart() for the IDLE command"});
            }
            if constexpr (std::is_same_v<CommandData, command::Authenticate>) {
                if (!request.data.initial_response.has_value()) {
                    co_return std::unexpected(ClientError{
                            .code = ClientError::CODE::UNSUPPORTED,
                            .message = "AUTHENTICATE without initial response is not implemented yet"});
                }
            }

            auto wire = wire::CommandSerializer::serialize(request);
            if (!wire) {
                co_return std::unexpected(ClientError{.code = ClientError::CODE::PROTOCOL_STATE,
                                                      .message = "failed to serialize IMAP command",
                                                      .core_error = wire.error()});
            }

            auto wrote = co_await sendRaw(*wire);
            if (!wrote) { co_return std::unexpected(wrote.error()); }

            std::vector<response::ServerResponse> responses;
            while (true) {
                auto next = co_await readResponse();
                if (!next) { co_return std::unexpected(next.error()); }

                responses.push_back(*next);
                applyServerResponse(*next);

                if (const auto *tagged = std::get_if<response::TaggedStatus>(&responses.back());
                    tagged != nullptr && tagged->tag == request.tag) {
                    applyCommandCompletion<CommandData>(tagged->data);
                    break;
                }
            }

            co_return responses;
        }

        usub::uvent::task::Awaitable<std::expected<std::vector<response::ServerResponse>, ClientError>>
        idleStart(command::Idle data = {}) {
            if (!connected_) {
                co_return std::unexpected(
                        ClientError{.code = ClientError::CODE::INVALID_INPUT, .message = "client is not connected"});
            }
            if (idle_active_) {
                co_return std::unexpected(
                        ClientError{.code = ClientError::CODE::PROTOCOL_STATE, .message = "IDLE is already active"});
            }

            auto tag = nextTag();
            if (!tag) {
                co_return std::unexpected(ClientError{.code = ClientError::CODE::PROTOCOL_STATE,
                                                      .message = "failed to allocate IMAP tag",
                                                      .core_error = tag.error()});
            }

            command::Command<command::Idle> request{.tag = *tag, .data = std::move(data)};
            auto wire = wire::CommandSerializer::serialize(request);
            if (!wire) {
                co_return std::unexpected(ClientError{.code = ClientError::CODE::PROTOCOL_STATE,
                                                      .message = "failed to serialize IDLE command",
                                                      .core_error = wire.error()});
            }

            auto wrote = co_await sendRaw(*wire);
            if (!wrote) { co_return std::unexpected(wrote.error()); }

            std::vector<response::ServerResponse> responses;
            while (true) {
                auto next = co_await readResponse();
                if (!next) { co_return std::unexpected(next.error()); }

                responses.push_back(*next);
                applyServerResponse(*next);

                if (std::holds_alternative<response::Continuation<>>(responses.back())) {
                    idle_active_ = true;
                    idle_tag_ = *tag;
                    break;
                }

                if (const auto *tagged = std::get_if<response::TaggedStatus>(&responses.back());
                    tagged != nullptr && tagged->tag == *tag) {
                    applyCommandCompletion<command::Idle>(tagged->data);
                    break;
                }
            }

            co_return responses;
        }

        usub::uvent::task::Awaitable<std::expected<std::vector<response::ServerResponse>, ClientError>> idleDone() {
            if (!connected_) {
                co_return std::unexpected(
                        ClientError{.code = ClientError::CODE::INVALID_INPUT, .message = "client is not connected"});
            }
            if (!idle_active_ || !idle_tag_.has_value()) {
                co_return std::unexpected(
                        ClientError{.code = ClientError::CODE::PROTOCOL_STATE, .message = "IDLE is not active"});
            }

            auto wrote = co_await sendRaw("DONE\r\n");
            if (!wrote) { co_return std::unexpected(wrote.error()); }

            std::vector<response::ServerResponse> responses;
            while (true) {
                auto next = co_await readResponse();
                if (!next) { co_return std::unexpected(next.error()); }

                responses.push_back(*next);
                applyServerResponse(*next);

                if (const auto *tagged = std::get_if<response::TaggedStatus>(&responses.back());
                    tagged != nullptr && tagged->tag == *idle_tag_) {
                    idle_active_ = false;
                    idle_tag_.reset();
                    break;
                }
            }

            co_return responses;
        }

        usub::uvent::task::Awaitable<std::expected<response::Capability, ClientError>>
        capability(command::Capability data = {}) {
            auto raw = co_await command(std::move(data));
            if (!raw) { co_return std::unexpected(raw.error()); }

            auto completion = requireOkCompletion(*raw, "CAPABILITY");
            if (!completion) { co_return std::unexpected(completion.error()); }

            response::Capability result{};
            for (const auto &entry: *raw) {
                if (const auto *untagged = std::get_if<response::UntaggedServerData>(&entry)) {
                    if (const auto *capability = std::get_if<response::CapabilityData>(&untagged->data)) {
                        appendCapabilities(result.capabilities, capability->capabilities);
                    }
                }
            }
            co_return result;
        }

        usub::uvent::task::Awaitable<std::expected<response::Noop, ClientError>>
        noop(command::Noop data = {}) {
            auto raw = co_await command(std::move(data));
            if (!raw) { co_return std::unexpected(raw.error()); }
            auto completion = requireOkCompletion(*raw, "NOOP");
            if (!completion) { co_return std::unexpected(completion.error()); }
            co_return response::Noop{};
        }

        usub::uvent::task::Awaitable<std::expected<response::Logout, ClientError>>
        logout(command::Logout data = {}) {
            auto raw = co_await command(std::move(data));
            if (!raw) { co_return std::unexpected(raw.error()); }
            auto completion = requireOkCompletion(*raw, "LOGOUT");
            if (!completion) { co_return std::unexpected(completion.error()); }
            co_return response::Logout{};
        }

        usub::uvent::task::Awaitable<std::expected<response::StartTls, ClientError>>
        startTls(command::StartTls data = {}) {
            auto raw = co_await command(std::move(data));
            if (!raw) { co_return std::unexpected(raw.error()); }
            auto completion = requireOkCompletion(*raw, "STARTTLS");
            if (!completion) { co_return std::unexpected(completion.error()); }
            co_return response::StartTls{};
        }

        usub::uvent::task::Awaitable<std::expected<response::Authenticate, ClientError>>
        authenticate(command::Authenticate data) {
            auto raw = co_await command(std::move(data));
            if (!raw) { co_return std::unexpected(raw.error()); }
            auto completion = requireOkCompletion(*raw, "AUTHENTICATE");
            if (!completion) { co_return std::unexpected(completion.error()); }
            co_return response::Authenticate{};
        }

        usub::uvent::task::Awaitable<std::expected<response::Login, ClientError>>
        login(command::Login data) {
            auto raw = co_await command(std::move(data));
            if (!raw) { co_return std::unexpected(raw.error()); }
            auto completion = requireOkCompletion(*raw, "LOGIN");
            if (!completion) { co_return std::unexpected(completion.error()); }
            co_return response::Login{};
        }

        usub::uvent::task::Awaitable<std::expected<response::Enable, ClientError>>
        enable(command::Enable data) {
            auto raw = co_await command(std::move(data));
            if (!raw) { co_return std::unexpected(raw.error()); }
            auto completion = requireOkCompletion(*raw, "ENABLE");
            if (!completion) { co_return std::unexpected(completion.error()); }

            response::Enable result{};
            for (const auto &entry: *raw) {
                if (const auto *untagged = std::get_if<response::UntaggedServerData>(&entry)) {
                    if (const auto *enabled = std::get_if<response::EnabledData>(&untagged->data)) {
                        appendCapabilities(result.enabled_capabilities, enabled->capabilities);
                    }
                }
            }
            co_return result;
        }

        usub::uvent::task::Awaitable<std::expected<response::Select, ClientError>>
        select(command::Select data) {
            auto raw = co_await command(std::move(data));
            if (!raw) { co_return std::unexpected(raw.error()); }
            auto completion = requireOkCompletion(*raw, "SELECT");
            if (!completion) { co_return std::unexpected(completion.error()); }

            response::Select result{};
            for (const auto &entry: *raw) {
                if (const auto *untagged_data = std::get_if<response::UntaggedServerData>(&entry)) {
                    std::visit(
                            [&](const auto &item) {
                                using T = std::decay_t<decltype(item)>;
                                if constexpr (std::is_same_v<T, response::FlagsData>) {
                                    result.flags = item.flags;
                                } else if constexpr (std::is_same_v<T, response::Exists>) {
                                    result.exists = item.count;
                                } else if constexpr (std::is_same_v<T, response::Recent>) {
                                    result.recent = item.count;
                                }
                            },
                            untagged_data->data);
                } else if (const auto *untagged_status = std::get_if<response::UntaggedStatus>(&entry)) {
                    std::visit([&](const auto &status) {
                        if (status.code.has_value()) { applySelectCode(result, *status.code); }
                    }, untagged_status->data);
                } else if (const auto *tagged = std::get_if<response::TaggedStatus>(&entry)) {
                    std::visit([&](const auto &status) {
                        if (status.code.has_value()) { applySelectCode(result, *status.code); }
                    }, tagged->data);
                }
            }
            co_return result;
        }

        usub::uvent::task::Awaitable<std::expected<response::Examine, ClientError>>
        examine(command::Examine data) {
            auto raw = co_await command(std::move(data));
            if (!raw) { co_return std::unexpected(raw.error()); }
            auto completion = requireOkCompletion(*raw, "EXAMINE");
            if (!completion) { co_return std::unexpected(completion.error()); }

            response::Examine result{};
            for (const auto &entry: *raw) {
                if (const auto *untagged_data = std::get_if<response::UntaggedServerData>(&entry)) {
                    std::visit(
                            [&](const auto &item) {
                                using T = std::decay_t<decltype(item)>;
                                if constexpr (std::is_same_v<T, response::FlagsData>) {
                                    result.flags = item.flags;
                                } else if constexpr (std::is_same_v<T, response::Exists>) {
                                    result.exists = item.count;
                                } else if constexpr (std::is_same_v<T, response::Recent>) {
                                    result.recent = item.count;
                                }
                            },
                            untagged_data->data);
                } else if (const auto *untagged_status = std::get_if<response::UntaggedStatus>(&entry)) {
                    std::visit([&](const auto &status) {
                        if (status.code.has_value()) { applySelectCode(result, *status.code); }
                    }, untagged_status->data);
                } else if (const auto *tagged = std::get_if<response::TaggedStatus>(&entry)) {
                    std::visit([&](const auto &status) {
                        if (status.code.has_value()) { applySelectCode(result, *status.code); }
                    }, tagged->data);
                }
            }
            co_return result;
        }

        usub::uvent::task::Awaitable<std::expected<response::Create, ClientError>>
        create(command::Create data) {
            auto raw = co_await command(std::move(data));
            if (!raw) { co_return std::unexpected(raw.error()); }
            auto completion = requireOkCompletion(*raw, "CREATE");
            if (!completion) { co_return std::unexpected(completion.error()); }
            co_return response::Create{};
        }

        usub::uvent::task::Awaitable<std::expected<response::Delete, ClientError>>
        deleteMailbox(command::Delete data) {
            auto raw = co_await command(std::move(data));
            if (!raw) { co_return std::unexpected(raw.error()); }
            auto completion = requireOkCompletion(*raw, "DELETE");
            if (!completion) { co_return std::unexpected(completion.error()); }
            co_return response::Delete{};
        }

        usub::uvent::task::Awaitable<std::expected<response::Rename, ClientError>>
        rename(command::Rename data) {
            auto raw = co_await command(std::move(data));
            if (!raw) { co_return std::unexpected(raw.error()); }
            auto completion = requireOkCompletion(*raw, "RENAME");
            if (!completion) { co_return std::unexpected(completion.error()); }
            co_return response::Rename{};
        }

        usub::uvent::task::Awaitable<std::expected<response::Subscribe, ClientError>>
        subscribe(command::Subscribe data) {
            auto raw = co_await command(std::move(data));
            if (!raw) { co_return std::unexpected(raw.error()); }
            auto completion = requireOkCompletion(*raw, "SUBSCRIBE");
            if (!completion) { co_return std::unexpected(completion.error()); }
            co_return response::Subscribe{};
        }

        usub::uvent::task::Awaitable<std::expected<response::Unsubscribe, ClientError>>
        unsubscribe(command::Unsubscribe data) {
            auto raw = co_await command(std::move(data));
            if (!raw) { co_return std::unexpected(raw.error()); }
            auto completion = requireOkCompletion(*raw, "UNSUBSCRIBE");
            if (!completion) { co_return std::unexpected(completion.error()); }
            co_return response::Unsubscribe{};
        }

        usub::uvent::task::Awaitable<std::expected<response::List, ClientError>>
        list(command::List data) {
            auto raw = co_await command(std::move(data));
            if (!raw) { co_return std::unexpected(raw.error()); }
            auto completion = requireOkCompletion(*raw, "LIST");
            if (!completion) { co_return std::unexpected(completion.error()); }

            response::List result{};
            for (const auto &entry: *raw) {
                if (const auto *untagged = std::get_if<response::UntaggedServerData>(&entry)) {
                    if (const auto *item = std::get_if<response::ListData>(&untagged->data)) {
                        result.mailboxes.push_back(response::ListedMailbox{
                                .attributes = item->item.attributes,
                                .hierarchy_delimiter = item->item.hierarchy_delimiter,
                                .mailbox_name = item->item.mailbox_name,
                        });
                    }
                }
            }
            co_return result;
        }

        usub::uvent::task::Awaitable<std::expected<response::Namespace, ClientError>>
        namespaceCommand(command::Namespace data = {}) {
            auto raw = co_await command(std::move(data));
            if (!raw) { co_return std::unexpected(raw.error()); }
            auto completion = requireOkCompletion(*raw, "NAMESPACE");
            if (!completion) { co_return std::unexpected(completion.error()); }

            response::Namespace result{};
            for (const auto &entry: *raw) {
                if (const auto *untagged = std::get_if<response::UntaggedServerData>(&entry)) {
                    if (const auto *ns = std::get_if<response::NamespaceData>(&untagged->data)) {
                        result.personal = ns->personal;
                        result.other_users = ns->other_users;
                        result.shared = ns->shared;
                    }
                }
            }
            co_return result;
        }

        usub::uvent::task::Awaitable<std::expected<response::StatusResult, ClientError>>
        statusCommand(command::Status data) {
            auto raw = co_await command(std::move(data));
            if (!raw) { co_return std::unexpected(raw.error()); }
            auto completion = requireOkCompletion(*raw, "STATUS");
            if (!completion) { co_return std::unexpected(completion.error()); }

            response::StatusResult result{};
            for (const auto &entry: *raw) {
                if (const auto *untagged = std::get_if<response::UntaggedServerData>(&entry)) {
                    if (const auto *status = std::get_if<response::StatusData>(&untagged->data)) {
                        result.mailbox_name = status->mailbox_name;
                        result.attributes = status->attributes;
                    }
                }
            }
            co_return result;
        }

        usub::uvent::task::Awaitable<std::expected<response::Append, ClientError>>
        append(command::Append data) {
            auto raw = co_await command(std::move(data));
            if (!raw) { co_return std::unexpected(raw.error()); }
            auto completion = requireOkCompletion(*raw, "APPEND");
            if (!completion) { co_return std::unexpected(completion.error()); }

            response::Append result{};
            fillAppendResult(result, *completion);
            co_return result;
        }

        usub::uvent::task::Awaitable<std::expected<response::Check, ClientError>>
        check(command::Check data = {}) {
            auto raw = co_await command(std::move(data));
            if (!raw) { co_return std::unexpected(raw.error()); }
            auto completion = requireOkCompletion(*raw, "CHECK");
            if (!completion) { co_return std::unexpected(completion.error()); }
            co_return response::Check{};
        }

        usub::uvent::task::Awaitable<std::expected<response::Close, ClientError>>
        closeMailbox(command::Close data = {}) {
            auto raw = co_await command(std::move(data));
            if (!raw) { co_return std::unexpected(raw.error()); }
            auto completion = requireOkCompletion(*raw, "CLOSE");
            if (!completion) { co_return std::unexpected(completion.error()); }
            co_return response::Close{};
        }

        usub::uvent::task::Awaitable<std::expected<response::Unselect, ClientError>>
        unselect(command::Unselect data = {}) {
            auto raw = co_await command(std::move(data));
            if (!raw) { co_return std::unexpected(raw.error()); }
            auto completion = requireOkCompletion(*raw, "UNSELECT");
            if (!completion) { co_return std::unexpected(completion.error()); }
            co_return response::Unselect{};
        }

        usub::uvent::task::Awaitable<std::expected<response::ExpungeResult, ClientError>>
        expunge(command::Expunge data = {}) {
            auto raw = co_await command(std::move(data));
            if (!raw) { co_return std::unexpected(raw.error()); }
            auto completion = requireOkCompletion(*raw, "EXPUNGE");
            if (!completion) { co_return std::unexpected(completion.error()); }
            co_return response::ExpungeResult{};
        }

        usub::uvent::task::Awaitable<std::expected<response::Search, ClientError>>
        search(command::Search data) {
            auto raw = co_await command(std::move(data));
            if (!raw) { co_return std::unexpected(raw.error()); }
            auto completion = requireOkCompletion(*raw, "SEARCH");
            if (!completion) { co_return std::unexpected(completion.error()); }

            response::Search result{};
            for (const auto &entry: *raw) {
                if (const auto *untagged = std::get_if<response::UntaggedServerData>(&entry)) {
                    if (const auto *search = std::get_if<response::SearchData>(&untagged->data)) {
                        result.matches = search->matches;
                        result.min = search->min;
                        result.max = search->max;
                        result.count = search->count;
                    }
                }
            }
            co_return result;
        }

        usub::uvent::task::Awaitable<std::expected<response::Fetch, ClientError>>
        fetch(command::Fetch data) {
            auto raw = co_await command(std::move(data));
            if (!raw) { co_return std::unexpected(raw.error()); }
            auto completion = requireOkCompletion(*raw, "FETCH");
            if (!completion) { co_return std::unexpected(completion.error()); }

            response::Fetch result{};
            for (const auto &entry: *raw) {
                if (const auto *untagged = std::get_if<response::UntaggedServerData>(&entry)) {
                    if (const auto *fetch_data = std::get_if<response::FetchData>(&untagged->data)) {
                        result.messages.push_back(response::FetchedMessage{
                                .sequence_number = fetch_data->sequence_number,
                                .items = fetch_data->items,
                        });
                    }
                }
            }
            co_return result;
        }

        usub::uvent::task::Awaitable<std::expected<response::Store, ClientError>>
        store(command::Store data) {
            auto raw = co_await command(std::move(data));
            if (!raw) { co_return std::unexpected(raw.error()); }
            auto completion = requireOkCompletion(*raw, "STORE");
            if (!completion) { co_return std::unexpected(completion.error()); }

            response::Store result{};
            for (const auto &entry: *raw) {
                if (const auto *untagged = std::get_if<response::UntaggedServerData>(&entry)) {
                    if (const auto *fetch_data = std::get_if<response::FetchData>(&untagged->data)) {
                        result.updated_messages.push_back(response::FetchedMessage{
                                .sequence_number = fetch_data->sequence_number,
                                .items = fetch_data->items,
                        });
                    }
                }
            }
            co_return result;
        }

        usub::uvent::task::Awaitable<std::expected<response::Copy, ClientError>>
        copy(command::Copy data) {
            auto raw = co_await command(std::move(data));
            if (!raw) { co_return std::unexpected(raw.error()); }
            auto completion = requireOkCompletion(*raw, "COPY");
            if (!completion) { co_return std::unexpected(completion.error()); }

            response::Copy result{};
            fillCopyLikeResult(result, *completion);
            co_return result;
        }

        usub::uvent::task::Awaitable<std::expected<response::Move, ClientError>>
        move(command::Move data) {
            auto raw = co_await command(std::move(data));
            if (!raw) { co_return std::unexpected(raw.error()); }
            auto completion = requireOkCompletion(*raw, "MOVE");
            if (!completion) { co_return std::unexpected(completion.error()); }

            response::Move result{};
            fillCopyLikeResult(result, *completion);
            co_return result;
        }

        usub::uvent::task::Awaitable<void> close() {
            if (!connected_ || !socket_.has_value()) { co_return; }

            if (use_ssl_) {
                using SslStream = typename first_stream_by_ssl<true, Streams...>::type;
                if constexpr (!std::is_void_v<SslStream>) {
                    auto &stream_instance = std::get<SslStream>(streams_);
                    co_await stream_instance.shutdown(*socket_);
                }
            } else {
                using PlainStream = typename first_stream_by_ssl<false, Streams...>::type;
                if constexpr (!std::is_void_v<PlainStream>) {
                    auto &stream_instance = std::get<PlainStream>(streams_);
                    co_await stream_instance.shutdown(*socket_);
                }
            }

            socket_.reset();
            connected_ = false;
            status_ = STATUS::DISCONNECTED;
            idle_active_ = false;
            idle_tag_.reset();
            co_return;
        }

    private:
        template<typename T, typename = void>
        struct stream_is_ssl : std::false_type {};

        template<typename T>
        struct stream_is_ssl<T, std::void_t<decltype(T::ssl)>> : std::bool_constant<static_cast<bool>(T::ssl)> {};

        template<bool WantSsl, typename... Ts>
        struct first_stream_by_ssl {
            using type = void;
        };

        template<bool WantSsl, typename T, typename... Ts>
        struct first_stream_by_ssl<WantSsl, T, Ts...> {
            using type = std::conditional_t<stream_is_ssl<T>::value == WantSsl, T,
                                            typename first_stream_by_ssl<WantSsl, Ts...>::type>;
        };

        [[nodiscard]] static bool asciiEqual(std::string_view lhs, std::string_view rhs) noexcept {
            if (lhs.size() != rhs.size()) { return false; }
            for (std::size_t i = 0; i < lhs.size(); ++i) {
                const auto left = static_cast<unsigned char>(lhs[i]);
                const auto right = static_cast<unsigned char>(rhs[i]);
                if (std::tolower(left) != std::tolower(right)) { return false; }
            }
            return true;
        }

        [[nodiscard]] std::expected<std::string, Error> nextTag() {
            if (tag_counter_ > options_.max_tag_counter) {
                return std::unexpected(Error{.code = Error::CODE::LIMIT_EXCEEDED,
                                             .message = "imap client tag counter exhausted"});
            }

            std::string tag = options_.tag_prefix;
            tag.append(std::to_string(tag_counter_));
            ++tag_counter_;
            return tag;
        }

        void resetProtocolState() {
            parser_.reset();
            greeting_.reset();
            capabilities_.clear();
            tag_counter_ = options_.initial_tag_counter;
            status_ = STATUS::DISCONNECTED;
            idle_active_ = false;
            idle_tag_.reset();
        }

        static bool isOk(const response::TaggedStatusData &status) {
            return std::holds_alternative<response::Ok>(status);
        }

        static void appendUnique(std::vector<std::string> &target, std::string value) {
            if (std::find_if(target.begin(), target.end(), [&](const std::string &existing) {
                    return asciiEqual(existing, value);
                }) == target.end()) {
                target.push_back(std::move(value));
            }
        }

        static void appendCapabilities(std::vector<std::string> &target, const std::vector<std::string> &values) {
            for (const auto &value: values) { appendUnique(target, value); }
        }

        static void appendCapabilitiesFromCode(std::vector<std::string> &target, const response::ResponseCode &code) {
            if (!asciiEqual(code.name, "CAPABILITY") || !code.data.has_value()) { return; }
            std::string text = *code.data;
            std::size_t cursor = 0;
            while (cursor < text.size()) {
                while (cursor < text.size() && std::isspace(static_cast<unsigned char>(text[cursor])) != 0) { ++cursor; }
                const auto next = text.find(' ', cursor);
                const auto token = text.substr(cursor, next == std::string::npos ? next : next - cursor);
                if (!token.empty()) { appendUnique(target, token); }
                if (next == std::string::npos) { break; }
                cursor = next + 1;
            }
        }

        void applyServerResponse(const response::ServerResponse &value) {
            if (const auto *tagged = std::get_if<response::TaggedStatus>(&value)) {
                std::visit([&](const auto &status) {
                    if (status.code.has_value()) { appendCapabilitiesFromCode(capabilities_, *status.code); }
                }, tagged->data);
                return;
            }

            if (const auto *untagged_status = std::get_if<response::UntaggedStatus>(&value)) {
                std::visit(
                        [&](const auto &status) {
                            using T = std::decay_t<decltype(status)>;
                            if (status.code.has_value()) { appendCapabilitiesFromCode(capabilities_, *status.code); }
                            if constexpr (std::is_same_v<T, response::Ok>) {
                                if (status_ == STATUS::DISCONNECTED) { status_ = STATUS::NOT_AUTHENTICATED; }
                            } else if constexpr (std::is_same_v<T, response::Preauth>) {
                                status_ = STATUS::AUTHENTICATED;
                            } else if constexpr (std::is_same_v<T, response::Bye>) {
                                status_ = STATUS::LOGOUT;
                            } else if constexpr (std::is_same_v<T, response::GenericStatus>) {
                                if (asciiEqual(status.name, "BYE")) { status_ = STATUS::LOGOUT; }
                            }
                        },
                        untagged_status->data);
                return;
            }

            if (const auto *untagged = std::get_if<response::UntaggedServerData>(&value)) {
                std::visit(
                        [&](const auto &data) {
                            using T = std::decay_t<decltype(data)>;
                            if constexpr (std::is_same_v<T, response::CapabilityData>) {
                                appendCapabilities(capabilities_, data.capabilities);
                            } else if constexpr (std::is_same_v<T, response::EnabledData>) {
                                appendCapabilities(capabilities_, data.capabilities);
                            }
                        },
                        untagged->data);
            }
        }

        template<typename CommandData>
        void applyCommandCompletion(const response::TaggedStatusData &status) {
            if (!isOk(status)) { return; }

            if constexpr (std::is_same_v<CommandData, command::Login> || std::is_same_v<CommandData, command::Authenticate>) {
                status_ = STATUS::AUTHENTICATED;
            } else if constexpr (std::is_same_v<CommandData, command::Select>
                                 || std::is_same_v<CommandData, command::Examine>) {
                status_ = STATUS::SELECTED;
            } else if constexpr (std::is_same_v<CommandData, command::Close>
                                 || std::is_same_v<CommandData, command::Unselect>) {
                status_ = STATUS::AUTHENTICATED;
            } else if constexpr (std::is_same_v<CommandData, command::Logout>) {
                status_ = STATUS::LOGOUT;
            } else if constexpr (std::is_same_v<CommandData, command::Capability>) {
                if (status_ == STATUS::DISCONNECTED) { status_ = STATUS::NOT_AUTHENTICATED; }
            }
        }

        static std::expected<response::Completion, ClientError>
        extractCompletion(const std::vector<response::ServerResponse> &responses) {
            for (auto it = responses.rbegin(); it != responses.rend(); ++it) {
                if (const auto *tagged = std::get_if<response::TaggedStatus>(&*it)) {
                    return response::Completion{.tag = tagged->tag, .status = tagged->data};
                }
            }
            return std::unexpected(
                    ClientError{.code = ClientError::CODE::PROTOCOL_STATE, .message = "missing tagged IMAP completion"});
        }

        static ClientError completionError(std::string_view command_name, const response::TaggedStatusData &status) {
            return std::visit(
                    [&](const auto &value) -> ClientError {
                        using T = std::decay_t<decltype(value)>;

                        std::string message(command_name);
                        message.append(" failed");

                        if constexpr (std::is_same_v<T, response::No>) {
                            message.append(" with NO");
                        } else if constexpr (std::is_same_v<T, response::Bad>) {
                            message.append(" with BAD");
                        } else if constexpr (std::is_same_v<T, response::GenericStatus>) {
                            message.append(" with ");
                            message.append(value.name);
                        }

                        if (!value.text.empty()) {
                            message.append(": ");
                            message.append(value.text);
                        }

                        return ClientError{.code = ClientError::CODE::PROTOCOL_STATE, .message = std::move(message)};
                    },
                    status);
        }

        static std::expected<response::Completion, ClientError>
        requireOkCompletion(const std::vector<response::ServerResponse> &responses, std::string_view command_name) {
            auto completion = extractCompletion(responses);
            if (!completion) { return std::unexpected(completion.error()); }
            if (!std::holds_alternative<response::Ok>(completion->status)) {
                return std::unexpected(completionError(command_name, completion->status));
            }
            return *completion;
        }

        static void fillAppendResult(response::Append &target, const response::Completion &completion) {
            std::visit(
                    [&](const auto &status) {
                        if (!status.code.has_value() || !asciiEqual(status.code->name, "APPENDUID")
                            || !status.code->data.has_value()) {
                            return;
                        }
                        std::string text = *status.code->data;
                        const auto space = text.find(' ');
                        if (space == std::string::npos) { return; }
                        target.uid_validity = static_cast<std::uint32_t>(std::stoul(text.substr(0, space)));
                        target.assigned_uid = static_cast<std::uint32_t>(std::stoul(text.substr(space + 1)));
                    },
                    completion.status);
        }

        template<typename CopyLike>
        static void fillCopyLikeResult(CopyLike &target, const response::Completion &completion) {
            std::visit(
                    [&](const auto &status) {
                        if (!status.code.has_value() || !asciiEqual(status.code->name, "COPYUID")
                            || !status.code->data.has_value()) {
                            return;
                        }
                        std::string text = *status.code->data;
                        const auto first = text.find(' ');
                        const auto second = first == std::string::npos ? std::string::npos : text.find(' ', first + 1);
                        if (first == std::string::npos || second == std::string::npos) { return; }
                        target.uid_validity = static_cast<std::uint32_t>(std::stoul(text.substr(0, first)));
                        target.source_uids = parseSequenceSet(text.substr(first + 1, second - first - 1));
                        target.destination_uids = parseSequenceSet(text.substr(second + 1));
                    },
                    completion.status);
        }

        template<typename SelectLike>
        static void applySelectCode(SelectLike &target, const response::ResponseCode &code) {
            if (asciiEqual(code.name, "READ-ONLY")) {
                target.read_only = true;
            } else if (asciiEqual(code.name, "READ-WRITE")) {
                target.read_only = false;
            } else if (asciiEqual(code.name, "UIDNEXT") && code.data.has_value()) {
                target.uid_next = static_cast<std::uint32_t>(std::stoul(*code.data));
            } else if (asciiEqual(code.name, "UIDVALIDITY") && code.data.has_value()) {
                target.uid_validity = static_cast<std::uint32_t>(std::stoul(*code.data));
            } else if (asciiEqual(code.name, "UNSEEN") && code.data.has_value()) {
                target.unseen = static_cast<std::uint32_t>(std::stoul(*code.data));
            } else if (asciiEqual(code.name, "PERMANENTFLAGS") && code.data.has_value()) {
                const auto open = code.data->find('(');
                const auto close = code.data->rfind(')');
                if (open != std::string::npos && close != std::string::npos && close > open) {
                    std::size_t cursor = open + 1;
                    while (cursor < close) {
                        while (cursor < close && std::isspace(static_cast<unsigned char>((*code.data)[cursor])) != 0) { ++cursor; }
                        const auto next = code.data->find(' ', cursor);
                        const auto end = (next == std::string::npos || next > close) ? close : next;
                        const auto token = code.data->substr(cursor, end - cursor);
                        if (!token.empty()) { target.permanent_flags.push_back(token); }
                        cursor = end + 1;
                    }
                }
            }
        }

        static std::optional<SequenceSet> parseSequenceSet(std::string_view value) {
            SequenceSet result{};
            if (value.empty()) { return result; }
            std::size_t cursor = 0;
            while (cursor < value.size()) {
                const auto comma = value.find(',', cursor);
                const auto token = value.substr(cursor, comma == std::string::npos ? comma : comma - cursor);
                const auto colon = token.find(':');
                auto parse_bound = [](std::string_view part) -> SequenceSetValue {
                    if (part == "*") { return std::monostate{}; }
                    return static_cast<std::uint32_t>(std::stoul(std::string(part)));
                };

                SequenceSetItem item{};
                if (colon == std::string_view::npos) {
                    item.first = parse_bound(token);
                } else {
                    item.first = parse_bound(token.substr(0, colon));
                    item.second = parse_bound(token.substr(colon + 1));
                }
                result.push_back(std::move(item));
                if (comma == std::string::npos) { break; }
                cursor = comma + 1;
            }
            return result;
        }

        usub::uvent::task::Awaitable<std::expected<void, ClientError>> sendRaw(std::string_view data) {
            if (!socket_.has_value()) {
                co_return std::unexpected(
                        ClientError{.code = ClientError::CODE::INVALID_INPUT, .message = "client socket is not initialized"});
            }

            if (use_ssl_) {
                using SslStream = typename first_stream_by_ssl<true, Streams...>::type;
                if constexpr (std::is_void_v<SslStream>) {
                    co_return std::unexpected(
                            ClientError{.code = ClientError::CODE::STREAM_UNAVAILABLE,
                                        .message = "no TLS stream configured for IMAPS"});
                } else {
                    auto &stream_instance = std::get<SslStream>(streams_);
                    co_await stream_instance.send(*socket_, data);
                    co_return {};
                }
            }

            using PlainStream = typename first_stream_by_ssl<false, Streams...>::type;
            if constexpr (std::is_void_v<PlainStream>) {
                co_return std::unexpected(
                        ClientError{.code = ClientError::CODE::STREAM_UNAVAILABLE,
                                    .message = "no plaintext stream configured for IMAP"});
            } else {
                auto &stream_instance = std::get<PlainStream>(streams_);
                co_await stream_instance.send(*socket_, data);
                co_return {};
            }
        }

        usub::uvent::task::Awaitable<std::expected<ssize_t, ClientError>> readRaw() {
            if (!socket_.has_value()) {
                co_return std::unexpected(
                        ClientError{.code = ClientError::CODE::INVALID_INPUT, .message = "client socket is not initialized"});
            }

            if (use_ssl_) {
                using SslStream = typename first_stream_by_ssl<true, Streams...>::type;
                if constexpr (std::is_void_v<SslStream>) {
                    co_return std::unexpected(
                            ClientError{.code = ClientError::CODE::STREAM_UNAVAILABLE,
                                        .message = "no TLS stream configured for IMAPS"});
                } else {
                    auto &stream_instance = std::get<SslStream>(streams_);
                    co_return co_await stream_instance.read(*socket_, read_buffer_);
                }
            }

            using PlainStream = typename first_stream_by_ssl<false, Streams...>::type;
            if constexpr (std::is_void_v<PlainStream>) {
                co_return std::unexpected(
                        ClientError{.code = ClientError::CODE::STREAM_UNAVAILABLE,
                                    .message = "no plaintext stream configured for IMAP"});
            } else {
                auto &stream_instance = std::get<PlainStream>(streams_);
                co_return co_await stream_instance.read(*socket_, read_buffer_);
            }
        }

        std::tuple<Streams...> streams_{};
        ClientOptions options_{};
        std::uint32_t tag_counter_{1};
        wire::ResponseParser parser_{};
        std::optional<response::ServerResponse> greeting_{};
        std::vector<std::string> capabilities_{};
        std::optional<usub::uvent::net::TCPClientSocket> socket_{};
        usub::uvent::utils::DynamicBuffer read_buffer_{};
        STATUS status_{STATUS::DISCONNECTED};
        bool connected_{false};
        bool use_ssl_{false};
        bool idle_active_{false};
        std::optional<std::string> idle_tag_{};
    };

}// namespace usub::unet::mail::imap
