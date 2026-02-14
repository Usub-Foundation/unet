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
        bool close_after_response{true};
        std::optional<std::string> host_header{};
    };

    template<typename... Streams>
    class ClientImpl {
    public:
        ClientImpl() = default;
        explicit ClientImpl(Streams... streams) : streams_(std::move(streams)...) {}
        ~ClientImpl() = default;

        template<typename Stream>
        Stream &stream() {
            static_assert((std::is_same_v<Stream, Streams> || ...),
                          "Requested stream is not part of this ClientImpl specialization");
            return std::get<Stream>(this->streams_);
        }

        template<typename Stream>
        usub::uvent::task::Awaitable<std::expected<Response, ClientError>>
        request(const std::string &host, std::uint16_t port, Request request,
                const ClientRequestOptions &options = ClientRequestOptions{}) {
            static_assert((std::is_same_v<Stream, Streams> || ...),
                          "Requested stream is not part of this ClientImpl specialization");
            auto &stream_instance = std::get<Stream>(this->streams_);
            auto response = co_await this->requestWithStream(stream_instance, host, port, std::move(request), options);
            co_return response;
        }

        usub::uvent::task::Awaitable<std::expected<Response, ClientError>>
        request(const std::string &host, std::uint16_t port, Request request,
                const ClientRequestOptions &options = ClientRequestOptions{})
            requires(sizeof...(Streams) > 0)
        {
            using DefaultStream = std::tuple_element_t<0, std::tuple<Streams...>>;
            auto response = co_await this->request<DefaultStream>(host, port, std::move(request), options);
            co_return response;
        }

    private:
        template<typename Stream>
        usub::uvent::task::Awaitable<std::expected<Response, ClientError>>
        requestWithStream(Stream &stream_instance, const std::string &host, std::uint16_t port, Request request,
                          const ClientRequestOptions &options) {
            if (request.metadata.method_token.empty()) {
                co_return std::unexpected(
                        ClientError{.code = ClientError::CODE::INVALID_REQUEST, .message = "Missing request method"});
            }

            if (request.metadata.version != VERSION::HTTP_1_0 && request.metadata.version != VERSION::HTTP_1_1) {
                request.metadata.version = VERSION::HTTP_1_1;
            }
            if (request.metadata.uri.path.empty() && request.metadata.authority.empty()) {
                request.metadata.uri.path = "/";
            }

            if (!request.headers.contains("host")) {
                std::string host_value = options.host_header.value_or(host);
                if (host_value.empty() && !request.metadata.authority.empty()) {
                    host_value = request.metadata.authority;
                }
                if (host_value.empty() && !request.metadata.uri.authority.host.empty()) {
                    host_value = request.metadata.uri.authority.host;
                }
                if (!host_value.empty() && port != 80 && port != 443) {
                    host_value.push_back(':');
                    host_value.append(std::to_string(port));
                }
                if (!host_value.empty()) { request.headers.addHeader("host", host_value); }
            }

            const std::string serialized = v1::RequestSerializer::serialize(request);

            usub::uvent::net::TCPClientSocket socket{};
            std::string host_copy = host;
            std::string port_copy = std::to_string(port);
            auto connect_error = co_await socket.async_connect(host_copy, port_copy, options.connect_timeout);
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
                    if (options.close_after_response) { co_await stream_instance.shutdown(socket); }
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
                    if (options.close_after_response) { co_await stream_instance.shutdown(socket); }
                    co_return std::unexpected(ClientError{
                            .code = ClientError::CODE::PARSE_FAILED,
                            .message = "connection closed before response was complete",
                    });
                }

                std::string_view chunk{reinterpret_cast<const char *>(buffer.data()),
                                       static_cast<std::size_t>(read_size)};

                if (read_until_close_mode) {
                    if (chunk.size() > std::numeric_limits<std::size_t>::max() - response.body.size()) {
                        if (options.close_after_response) { co_await stream_instance.shutdown(socket); }
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
                    if (options.close_after_response) { co_await stream_instance.shutdown(socket); }
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

            if (options.close_after_response) { co_await stream_instance.shutdown(socket); }
            co_return response;
        }

        std::tuple<Streams...> streams_{};
    };
}// namespace usub::unet::http
