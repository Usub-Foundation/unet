#pragma once

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

#include <uvent/Uvent.h>

#include "unet/http/core/response.hpp"
#include "unet/http/v1/wire/response_parser.hpp"

namespace http_client_test_support {
    inline usub::uvent::task::Awaitable<bool> write_all(usub::uvent::net::TCPClientSocket socket,
                                                        std::string_view data) {
        std::size_t offset = 0;
        while (offset < data.size()) {
            const ssize_t written =
                    co_await socket.async_write(reinterpret_cast<uint8_t *>(const_cast<char *>(data.data() + offset)),
                                                data.size() - offset);
            if (written <= 0) { co_return false; }
            offset += static_cast<std::size_t>(written);
        }
        co_return true;
    }

    inline usub::uvent::task::Awaitable<std::optional<std::string>>
    read_http_headers(usub::uvent::net::TCPClientSocket socket, std::string &pending, std::size_t max_read_size = 16 * 1024,
                      int max_reads = 32) {
        usub::uvent::utils::DynamicBuffer buffer{};
        buffer.reserve(max_read_size);

        for (int i = 0; i < max_reads; ++i) {
            const std::size_t end = pending.find("\r\n\r\n");
            if (end != std::string::npos) {
                std::string headers = pending.substr(0, end + 4);
                pending.erase(0, end + 4);
                co_return headers;
            }

            buffer.clear();
            const ssize_t read_size = co_await socket.async_read(buffer, max_read_size);
            if (read_size <= 0) { co_return std::nullopt; }
            pending.append(reinterpret_cast<const char *>(buffer.data()), static_cast<std::size_t>(read_size));
        }

        co_return std::nullopt;
    }

    template<typename Stream>
    inline usub::uvent::task::Awaitable<std::optional<std::string>>
    read_tls_http_headers(Stream &stream, usub::uvent::net::TCPClientSocket socket, std::string &pending,
                          int max_reads = 32) {
        usub::uvent::utils::DynamicBuffer buffer{};
        buffer.reserve(16 * 1024);

        for (int i = 0; i < max_reads; ++i) {
            const std::size_t end = pending.find("\r\n\r\n");
            if (end != std::string::npos) {
                std::string headers = pending.substr(0, end + 4);
                pending.erase(0, end + 4);
                co_return headers;
            }

            const ssize_t read_size = co_await stream.read(socket, buffer);
            if (read_size <= 0) { co_return std::nullopt; }
            pending.append(reinterpret_cast<const char *>(buffer.data()), static_cast<std::size_t>(read_size));
        }

        co_return std::nullopt;
    }

    inline std::optional<std::string_view> find_header(std::string_view headers, std::string_view key) {
        std::size_t line_start = 0;
        std::string lowered_key;
        lowered_key.reserve(key.size());
        for (char ch: key) { lowered_key.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch)))); }

        while (line_start < headers.size()) {
            const std::size_t line_end = headers.find("\r\n", line_start);
            if (line_end == std::string_view::npos || line_end == line_start) { break; }

            std::string_view line = headers.substr(line_start, line_end - line_start);
            const std::size_t colon = line.find(':');
            if (colon != std::string_view::npos) {
                std::string lowered_name;
                lowered_name.reserve(colon);
                for (std::size_t i = 0; i < colon; ++i) {
                    lowered_name.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(line[i]))));
                }
                if (lowered_name == lowered_key) {
                    std::string_view value = line.substr(colon + 1);
                    while (!value.empty() &&
                           (value.front() == ' ' || value.front() == '\t' || value.front() == '\r')) {
                        value.remove_prefix(1);
                    }
                    return value;
                }
            }

            line_start = line_end + 2;
        }

        return std::nullopt;
    }

    inline bool starts_with(std::string_view value, std::string_view prefix) {
        return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
    }

    inline bool write_file(const std::filesystem::path &path, std::string_view text) {
        std::ofstream out(path, std::ios::binary);
        if (!out) { return false; }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        return out.good();
    }

    inline usub::uvent::task::Awaitable<std::optional<usub::unet::http::Response>>
    read_http_response(usub::uvent::net::TCPClientSocket socket, std::size_t max_read_size = 16 * 1024, int max_reads = 32) {
        usub::unet::http::Response response{};
        usub::unet::http::v1::ResponseParser parser{};
        usub::uvent::utils::DynamicBuffer buffer{};
        buffer.reserve(max_read_size);

        bool read_until_close_mode = false;
        for (int i = 0; i < max_reads; ++i) {
            buffer.clear();
            const ssize_t read_size = co_await socket.async_read(buffer, max_read_size);
            if (read_size < 0) { co_return std::nullopt; }
            if (read_size == 0) {
                if (read_until_close_mode || parser.getContext().state == usub::unet::http::v1::ResponseParser::STATE::COMPLETE ||
                    parser.getContext().state == usub::unet::http::v1::ResponseParser::STATE::BODY_UNTIL_CLOSE) {
                    co_return response;
                }
                co_return std::nullopt;
            }

            std::string_view chunk{reinterpret_cast<const char *>(buffer.data()), static_cast<std::size_t>(read_size)};
            if (read_until_close_mode) {
                response.body.append(chunk.data(), chunk.size());
                continue;
            }

            auto begin = chunk.begin();
            const auto end = chunk.end();
            auto parsed = parser.parse(response, begin, end);
            if (!parsed) { co_return std::nullopt; }

            auto &ctx = parser.getContext();
            if (ctx.state == usub::unet::http::v1::ResponseParser::STATE::HEADERS_DONE &&
                ctx.after_headers == usub::unet::http::v1::ResponseParser::AfterHeaders::COMPLETE) {
                co_return response;
            }
            if (ctx.after_headers == usub::unet::http::v1::ResponseParser::AfterHeaders::UNTIL_CLOSE &&
                ctx.state == usub::unet::http::v1::ResponseParser::STATE::COMPLETE) {
                read_until_close_mode = true;
                continue;
            }
            if (ctx.state == usub::unet::http::v1::ResponseParser::STATE::COMPLETE) { co_return response; }
        }

        co_return std::nullopt;
    }
}// namespace http_client_test_support
