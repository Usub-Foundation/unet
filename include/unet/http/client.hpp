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

#include <iostream>

#include <uvent/Uvent.h>

#include "unet/http/request.hpp"
#include "unet/http/response.hpp"
#include "unet/http/v1/request_serializer.hpp"
#include "unet/http/v1/response_parser.hpp"

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
            std::cout << "[http-client] enter request<" << typeid(Stream).name() << ">\n";
            auto &stream_instance = std::get<Stream>(this->streams_);
            auto response = co_await this->requestWithStream(stream_instance, host, port, std::move(request), options);
            std::cout << "[http-client] await requestWithStream done\n";
            co_return response;
        }

        usub::uvent::task::Awaitable<std::expected<Response, ClientError>>
        request(const std::string &host, std::uint16_t port, Request request,
                const ClientRequestOptions &options = ClientRequestOptions{})
            requires(sizeof...(Streams) > 0)
        {
            using DefaultStream = std::tuple_element_t<0, std::tuple<Streams...>>;
            std::cout << "[http-client] enter request<DefaultStream=" << typeid(DefaultStream).name() << ">\n";
            auto response = co_await this->request<DefaultStream>(host, port, std::move(request), options);
            std::cout << "[http-client] await request<DefaultStream> done\n";
            co_return response;
        }

    private:
        template<typename Stream>
        usub::uvent::task::Awaitable<std::expected<Response, ClientError>>
        requestWithStream(Stream &stream_instance, const std::string &host, std::uint16_t port, Request request,
                          const ClientRequestOptions &options) {
            std::cout << "[http-client] enter requestWithStream<" << typeid(Stream).name() << "> host=" << host
                      << " port=" << port << '\n';
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
            std::cout << "[http-client] await connect done\n";
            if (connect_error.has_value()) {
                co_return std::unexpected(ClientError{
                        .code = ClientError::CODE::CONNECT_FAILED,
                        .message = "connect failed",
                });
            }

            co_await stream_instance.send(socket, serialized);
            std::cout << "[http-client] await send done\n";

            Response response{};
            v1::ResponseParser parser{};
            usub::uvent::utils::DynamicBuffer buffer{};
            bool read_until_close_mode = false;

            while (true) {
                const ssize_t read_size = co_await stream_instance.read(socket, buffer);
                std::cout << "[http-client] await read done: " << read_size << '\n';
                if (read_size < 0) {
                    if (options.close_after_response) {
                        co_await stream_instance.shutdown(socket);
                        std::cout << "[http-client] await shutdown done (read failure)\n";
                    }
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
                    if (options.close_after_response) {
                        co_await stream_instance.shutdown(socket);
                        std::cout << "[http-client] await shutdown done (premature close)\n";
                    }
                    co_return std::unexpected(ClientError{
                            .code = ClientError::CODE::PARSE_FAILED,
                            .message = "connection closed before response was complete",
                    });
                }

                std::string_view chunk{reinterpret_cast<const char *>(buffer.data()),
                                       static_cast<std::size_t>(read_size)};
                std::string escaped_chunk{};
                escaped_chunk.reserve(chunk.size());
                static constexpr char hex_digits[] = "0123456789abcdef";
                for (unsigned char byte: chunk) {
                    switch (byte) {
                        case '\r':
                            escaped_chunk += "\\r";
                            break;
                        case '\n':
                            escaped_chunk += "\\n";
                            break;
                        case '\t':
                            escaped_chunk += "\\t";
                            break;
                        case '\0':
                            escaped_chunk += "\\0";
                            break;
                        case '\\':
                            escaped_chunk += "\\\\";
                            break;
                        default:
                            if (std::isprint(byte) != 0) {
                                escaped_chunk.push_back(static_cast<char>(byte));
                            } else {
                                escaped_chunk += "\\x";
                                escaped_chunk.push_back(hex_digits[(byte >> 4U) & 0x0FU]);
                                escaped_chunk.push_back(hex_digits[byte & 0x0FU]);
                            }
                            break;
                    }
                }
                std::cout << escaped_chunk << '\n';

                if (read_until_close_mode) {
                    if (chunk.size() > std::numeric_limits<std::size_t>::max() - response.body.size() ||
                        response.body.size() + chunk.size() > response.policy.max_body_size) {
                        if (options.close_after_response) {
                            co_await stream_instance.shutdown(socket);
                            std::cout << "[http-client] await shutdown done (until-close max body)\n";
                        }
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
                const auto state_name = [](v1::ResponseParser::STATE state) {
                    using STATE = v1::ResponseParser::STATE;
                    switch (state) {
                        case STATE::STATUS_VERSION:
                            return "STATUS_VERSION";
                        case STATE::STATUS_CODE:
                            return "STATUS_CODE";
                        case STATE::STATUS_REASON:
                            return "STATUS_REASON";
                        case STATE::STATUS_LINE_CRLF:
                            return "STATUS_LINE_CRLF";
                        case STATE::HEADER_KEY:
                            return "HEADER_KEY";
                        case STATE::HEADER_VALUE:
                            return "HEADER_VALUE";
                        case STATE::HEADER_CR:
                            return "HEADER_CR";
                        case STATE::HEADER_LF:
                            return "HEADER_LF";
                        case STATE::HEADERS_CRLF:
                            return "HEADERS_CRLF";
                        case STATE::HEADERS_VALIDATION:
                            return "HEADERS_VALIDATION";
                        case STATE::HEADERS_DONE:
                            return "HEADERS_DONE";
                        case STATE::DATA_CONTENT_LENGTH:
                            return "DATA_CONTENT_LENGTH";
                        case STATE::DATA_CHUNKED_SIZE:
                            return "DATA_CHUNKED_SIZE";
                        case STATE::DATA_CHUNKED_SIZE_CRLF:
                            return "DATA_CHUNKED_SIZE_CRLF";
                        case STATE::DATA_CHUNKED_DATA:
                            return "DATA_CHUNKED_DATA";
                        case STATE::DATA_CHUNKED_DATA_CR:
                            return "DATA_CHUNKED_DATA_CR";
                        case STATE::DATA_CHUNKED_DATA_LF:
                            return "DATA_CHUNKED_DATA_LF";
                        case STATE::DATA_CHUNK_DONE:
                            return "DATA_CHUNK_DONE";
                        case STATE::DATA_CHUNKED_LAST_CR:
                            return "DATA_CHUNKED_LAST_CR";
                        case STATE::DATA_CHUNKED_LAST_LF:
                            return "DATA_CHUNKED_LAST_LF";
                        case STATE::DATA_DONE:
                            return "DATA_DONE";
                        case STATE::TRAILER_KEY:
                            return "TRAILER_KEY";
                        case STATE::TRAILER_VALUE:
                            return "TRAILER_VALUE";
                        case STATE::TRAILER_CR:
                            return "TRAILER_CR";
                        case STATE::TRAILER_LF:
                            return "TRAILER_LF";
                        case STATE::TRAILERS_DONE:
                            return "TRAILERS_DONE";
                        case STATE::BODY_UNTIL_CLOSE:
                            return "BODY_UNTIL_CLOSE";
                        case STATE::COMPLETE:
                            return "COMPLETE";
                        case STATE::FAILED:
                            return "FAILED";
                    }
                    return "UNKNOWN";
                };
                std::cout << "[http-client] parse " << (parsed ? "ok" : "err")
                          << " state=" << state_name(parser.getContext().state) << '\n';
                if (!parsed) {
                    if (options.close_after_response) {
                        co_await stream_instance.shutdown(socket);
                        std::cout << "[http-client] await shutdown done (parse failure)\n";
                    }
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

            if (options.close_after_response) {
                co_await stream_instance.shutdown(socket);
                std::cout << "[http-client] await shutdown done (normal)\n";
            }
            std::cout << "[http-client] returning\n";
            co_return response;
        }

        std::tuple<Streams...> streams_{};
    };
}// namespace usub::unet::http
