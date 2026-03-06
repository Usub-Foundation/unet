#pragma once

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <deque>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <uvent/Uvent.h>

#include "unet/mail/imap/core/command.hpp"
#include "unet/mail/imap/core/data_format.hpp"
#include "unet/mail/imap/core/encoder.hpp"
#include "unet/mail/imap/core/error.hpp"
#include "unet/mail/imap/core/parser.hpp"
#include "unet/mail/imap/core/state_machine.hpp"

namespace usub::unet::mail::imap {

    struct ClientSessionOptions {
        core::ParserLimits parser_limits{};
        std::string tag_prefix{"A"};
        std::uint32_t initial_tag_counter{1};
        std::uint32_t max_tag_counter{9999};
    };

    struct PendingCommand {
        std::string tag{};
        core::COMMAND command{core::COMMAND::NOOP};
    };

    class ClientSession {
    public:
        explicit ClientSession(ClientSessionOptions options = {});

        std::expected<std::string, core::Error>
        buildCommand(core::COMMAND command, std::vector<core::Value> arguments = {});

        std::expected<void, core::Error> feed(std::string_view bytes);
        std::expected<std::optional<core::Response>, core::Error> nextResponse();

        [[nodiscard]] const core::StateMachine &stateMachine() const noexcept;
        [[nodiscard]] core::SessionState state() const noexcept;
        [[nodiscard]] const std::deque<PendingCommand> &pendingCommands() const noexcept;

        void reset();

    private:
        std::expected<std::string, core::Error> nextTag();
        std::expected<void, core::Error> applyResponseToState(const core::Response &response);

        ClientSessionOptions options_{};
        core::ResponseParser parser_;
        core::StateMachine state_machine_{};
        std::deque<PendingCommand> pending_{};
        std::uint32_t tag_counter_{1};
        bool greeting_seen_{false};
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
        };

        CODE code{CODE::INVALID_INPUT};
        std::string message{};
        std::optional<core::Error> core_error{};
    };

    template<typename... Streams>
    class ClientImpl {
    public:
        template<typename T, typename = void>
        struct has_ssl_tag : std::false_type {};

        template<typename T>
        struct has_ssl_tag<T, std::void_t<decltype(T::ssl)>> : std::bool_constant<static_cast<bool>(T::ssl)> {};

        ClientImpl() = default;
        explicit ClientImpl(Streams... streams) : streams_(std::move(streams)...) {}

        [[nodiscard]] ClientSession &session() noexcept { return session_; }
        [[nodiscard]] const ClientSession &session() const noexcept { return session_; }
        [[nodiscard]] const std::optional<core::Response> &greeting() const noexcept { return greeting_; }

        template<typename Stream>
        Stream &stream() {
            static_assert((std::is_same_v<Stream, Streams> || ...),
                          "Requested stream is not part of this ClientImpl specialization");
            return std::get<Stream>(streams_);
        }

        usub::uvent::task::Awaitable<std::expected<void, ClientError>>
        connect(NetworkEndpoint endpoint, const ClientNetworkOptions &options = ClientNetworkOptions{}) {
            if (endpoint.host.empty()) {
                co_return std::unexpected(ClientError{.code = ClientError::CODE::INVALID_INPUT,
                                                      .message = "imap endpoint host must not be empty"});
            }

            std::string scheme = endpoint.scheme;
            std::transform(scheme.begin(), scheme.end(), scheme.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
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
            session_.reset();
            greeting_.reset();

            socket_.emplace();
            std::string connect_host = endpoint.host;
            std::string connect_port = std::to_string(endpoint.port);
            auto connect_error = co_await socket_->async_connect(connect_host, connect_port, options.connect_timeout);
            if (connect_error.has_value()) {
                socket_.reset();
                connected_ = false;
                co_return std::unexpected(
                        ClientError{.code = ClientError::CODE::CONNECT_FAILED, .message = "IMAP connect failed"});
            }

            connected_ = true;

            if (options.read_greeting) {
                auto greeting = co_await readResponse();
                if (!greeting) {
                    co_await close();
                    co_return std::unexpected(greeting.error());
                }
                greeting_ = *greeting;
            }

            co_return {};
        }

        usub::uvent::task::Awaitable<std::expected<core::Response, ClientError>> readResponse() {
            if (!connected_ || !socket_.has_value()) {
                co_return std::unexpected(ClientError{.code = ClientError::CODE::INVALID_INPUT,
                                                      .message = "client is not connected"});
            }

            while (true) {
                auto next = session_.nextResponse();
                if (!next) {
                    co_return std::unexpected(ClientError{.code = ClientError::CODE::PARSE_FAILED,
                                                          .message = "IMAP parse/session error",
                                                          .core_error = next.error()});
                }

                if (next->has_value()) { co_return std::move(**next); }

                auto read = co_await readRaw();
                if (!read) { co_return std::unexpected(read.error()); }
                if (*read <= 0) {
                    connected_ = false;
                    co_return std::unexpected(
                            ClientError{.code = ClientError::CODE::READ_FAILED,
                                        .message = "connection closed while waiting for IMAP response"});
                }

                std::string_view chunk{reinterpret_cast<const char *>(read_buffer_.data()),
                                       static_cast<std::size_t>(*read)};

                auto fed = session_.feed(chunk);
                if (!fed) {
                    co_return std::unexpected(ClientError{.code = ClientError::CODE::PARSE_FAILED,
                                                          .message = "failed feeding bytes to IMAP parser",
                                                          .core_error = fed.error()});
                }
            }
        }

        usub::uvent::task::Awaitable<std::expected<std::vector<core::Response>, ClientError>>
        command(core::COMMAND command, std::vector<core::Value> arguments = {}) {
            if (!connected_) {
                co_return std::unexpected(ClientError{.code = ClientError::CODE::INVALID_INPUT,
                                                      .message = "client is not connected"});
            }

            auto wire = session_.buildCommand(command, std::move(arguments));
            if (!wire) {
                co_return std::unexpected(ClientError{.code = ClientError::CODE::PROTOCOL_STATE,
                                                      .message = "failed to build IMAP command",
                                                      .core_error = wire.error()});
            }

            if (session_.pendingCommands().empty()) {
                co_return std::unexpected(ClientError{.code = ClientError::CODE::PROTOCOL_STATE,
                                                      .message = "no pending command after command build"});
            }

            const std::string expected_tag = session_.pendingCommands().back().tag;

            auto wrote = co_await sendRaw(*wire);
            if (!wrote) { co_return std::unexpected(wrote.error()); }

            std::vector<core::Response> responses;
            while (true) {
                auto next = co_await readResponse();
                if (!next) { co_return std::unexpected(next.error()); }

                responses.push_back(*next);
                const auto &response = responses.back();
                if (response.kind != core::Response::Kind::Tagged) { continue; }

                const auto &tagged = std::get<core::TaggedResponse>(response.data);
                if (tagged.tag.value == expected_tag) { break; }
            }

            co_return responses;
        }

        usub::uvent::task::Awaitable<std::expected<std::vector<core::Response>, ClientError>> capability() {
            co_return co_await command(core::COMMAND::CAPABILITY);
        }

        usub::uvent::task::Awaitable<std::expected<std::vector<core::Response>, ClientError>>
        login(std::string_view username, std::string_view password) {
            std::vector<core::Value> arguments{};
            arguments.push_back(
                    core::Value{.data = core::String{.form = core::String::Form::Quoted, .value = std::string(username)}});
            arguments.push_back(
                    core::Value{.data = core::String{.form = core::String::Form::Quoted, .value = std::string(password)}});
            co_return co_await command(core::COMMAND::LOGIN, std::move(arguments));
        }

        usub::uvent::task::Awaitable<std::expected<std::vector<core::Response>, ClientError>> logout() {
            co_return co_await command(core::COMMAND::LOGOUT);
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

        usub::uvent::task::Awaitable<std::expected<void, ClientError>> sendRaw(std::string_view data) {
            if (!socket_.has_value()) {
                co_return std::unexpected(ClientError{.code = ClientError::CODE::INVALID_INPUT,
                                                      .message = "client socket is not initialized"});
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
                co_return std::unexpected(ClientError{.code = ClientError::CODE::INVALID_INPUT,
                                                      .message = "client socket is not initialized"});
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
        ClientSession session_{};
        std::optional<core::Response> greeting_{};
        std::optional<usub::uvent::net::TCPClientSocket> socket_{};
        usub::uvent::utils::DynamicBuffer read_buffer_{};
        bool connected_{false};
        bool use_ssl_{false};
    };

}// namespace usub::unet::mail::imap
