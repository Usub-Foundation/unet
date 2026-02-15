#pragma once

#include <cctype>
#include <chrono>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <typeinfo>

#include <uvent/Uvent.h>

#include "unet/http/core/request.hpp"
#include "unet/http/core/response.hpp"
#include "unet/http/v1/wire/request_serializer.hpp"
#include "unet/http/v1/wire/response_parser.hpp"

namespace usub::unet::http {
    struct ClientError {
        enum class CODE : std::uint8_t {
            INVALID_REQUEST,
            CONNECT_FAILED,
            WRITE_FAILED,
            READ_FAILED,
            PARSE_FAILED,
            CLOSE_FAILED,
        };

        CODE code{CODE::INVALID_REQUEST};
        std::string message{};
        std::optional<ParseError> parse_error{};
    };

    struct ClientRequestOptions {
        std::chrono::milliseconds connect_timeout{std::chrono::milliseconds{3000}};
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
        ~ClientImpl() = default;

        static constexpr bool has_ssl_stream = (has_ssl_tag<Streams>::value || ...);

        template<typename Stream>
        Stream &stream() {
            static_assert((std::is_same_v<Stream, Streams> || ...),
                          "Requested stream is not part of this ClientImpl specialization");
            return std::get<Stream>(this->streams_);
        }


        usub::uvent::task::Awaitable<std::expected<Response, ClientError>>
        request(Request request, const ClientRequestOptions &options = ClientRequestOptions{}) {
            if (request.metadata.uri.scheme.empty()) {
                if constexpr (has_ssl_stream) {
                    request.metadata.uri.scheme = "https";
                } else {
                    request.metadata.uri.scheme = "http";
                }
            }

            if (request.metadata.authority.empty()) {
                co_return std::unexpected(ClientError{
                        .code = ClientError::CODE::INVALID_REQUEST,
                        .message = "missing authority field reuqest.metadata.authority.empty() == true",
                });
            }

            if (request.metadata.uri.scheme == "https") {
                using SslStream = typename first_stream_by_ssl<true, Streams...>::type;
                if constexpr (!std::is_void_v<SslStream>) {
                    auto &stream_instance = std::get<SslStream>(this->streams_);
                    auto response = co_await this->requestWithStream(stream_instance, std::move(request), options);
                    co_return response;
                } else {
                    using PlainStream = typename first_stream_by_ssl<false, Streams...>::type;
                    if constexpr (!std::is_void_v<PlainStream>) {
                        auto &stream_instance = std::get<PlainStream>(this->streams_);
                        auto response = co_await this->requestWithStream(stream_instance, std::move(request), options);
                        co_return response;
                    }
                }
            } else if (request.metadata.uri.scheme == "http") {
                using PlainStream = typename first_stream_by_ssl<false, Streams...>::type;
                if constexpr (!std::is_void_v<PlainStream>) {
                    auto &stream_instance = std::get<PlainStream>(this->streams_);
                    auto response = co_await this->requestWithStream(stream_instance, std::move(request), options);
                    co_return response;
                }
            }

            co_return std::unexpected(ClientError{
                    .code = ClientError::CODE::INVALID_REQUEST,
                    .message = "no compatible stream for scheme: " + request.metadata.uri.scheme,
            });
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

        template<typename Stream>
        usub::uvent::task::Awaitable<std::expected<Response, ClientError>>
        requestWithStream(Stream &stream_instance, Request request, const ClientRequestOptions &options) {
            if (request.metadata.method_token.empty()) { request.metadata.method_token = "GET"; }

            if (request.metadata.version != VERSION::HTTP_1_0 && request.metadata.version != VERSION::HTTP_1_1) {
                request.metadata.version = VERSION::HTTP_1_1;
            }
            if (request.metadata.uri.path.empty()) { request.metadata.uri.path = "/"; }

            if (!request.headers.contains("host")) {
                if (!request.metadata.uri.authority.host.empty()) {
                    request.headers.addHeader("host", request.metadata.authority);
                }
            }

            const std::string serialized = v1::RequestSerializer::serialize(request);

            usub::uvent::net::TCPClientSocket socket{};
            std::uint16_t port{};

            if constexpr (stream_is_ssl<Stream>::value) {
                port = 443;
            } else {
                port = 80;
            }

            std::string connect_host = request.metadata.uri.authority.host;
            const std::uint16_t connect_port_num =
                    request.metadata.uri.authority.port == 0 ? port : request.metadata.uri.authority.port;
            std::string connect_port = std::to_string(connect_port_num);

            auto connect_error = co_await socket.async_connect(connect_host, connect_port, options.connect_timeout);
            if (connect_error.has_value()) {
                co_return std::unexpected(ClientError{
                        .code = ClientError::CODE::CONNECT_FAILED,
                        .message = "connect failed",
                });
            }

            co_await stream_instance.send(socket, serialized);

            Response response{};
            v1::ResponseParser parser{};
            usub::uvent::utils::DynamicBuffer buffer{};
            bool read_until_close_mode = false;

            while (true) {
                const ssize_t read_size = co_await stream_instance.read(socket, buffer);

                if (read_size < 0) {
                    co_await stream_instance.shutdown(socket);
                    co_return std::unexpected(ClientError{
                            .code = ClientError::CODE::READ_FAILED,
                            .message = "read failed",
                    });
                }

                if (read_size == 0) {
                    if (read_until_close_mode || parser.getContext().state == v1::ResponseParser::STATE::COMPLETE ||
                        parser.getContext().state == v1::ResponseParser::STATE::BODY_UNTIL_CLOSE) {
                        break;
                    }
                    // if (options.close_after_response) { co_await stream_instance.shutdown(socket); }
                    co_return std::unexpected(ClientError{
                            .code = ClientError::CODE::PARSE_FAILED,
                            .message = "connection closed before response was complete",
                    });
                }

                std::string_view chunk{reinterpret_cast<const char *>(buffer.data()),
                                       static_cast<std::size_t>(read_size)};

                if (read_until_close_mode) {
                    if (chunk.size() > std::numeric_limits<std::size_t>::max() - response.body.size()) {
                        // if (options.close_after_response) { co_await stream_instance.shutdown(socket); }
                        co_return std::unexpected(ClientError{
                                .code = ClientError::CODE::PARSE_FAILED,
                                .message = "response body exceeded max size in until-close mode",
                        });
                    }
                    response.body.append(chunk.data(), chunk.size());
                    continue;
                }

                auto begin = chunk.begin();
                const auto end = chunk.end();
                auto parsed = parser.parse(response, begin, end);
                if (!parsed) {
                    // if (options.close_after_response) { co_await stream_instance.shutdown(socket); }
                    co_return std::unexpected(ClientError{
                            .code = ClientError::CODE::PARSE_FAILED,
                            .message = parsed.error().message,
                            .parse_error = parsed.error(),
                    });
                }

                auto &ctx = parser.getContext();
                if (ctx.after_headers == v1::ResponseParser::AfterHeaders::UNTIL_CLOSE &&
                    ctx.state == v1::ResponseParser::STATE::COMPLETE) {
                    read_until_close_mode = true;
                    continue;
                }

                if (ctx.state == v1::ResponseParser::STATE::COMPLETE) { break; }
            }

            // if (options.close_after_response) { co_await stream_instance.shutdown(socket); }
            co_return response;
        }

        std::tuple<Streams...> streams_{};
    };
}// namespace usub::unet::http
