#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

#include "unet/http/error.hpp"
#include "unet/http/response.hpp"

namespace usub::unet::http::v1 {

    class ResponseParser {
    public:
        enum class STATE : std::uint8_t {
            STATUS_LINE,
            HEADER_LINES,
            HEADERS_VALIDATION,
            DATA_CONTENT_LENGTH,
            DATA_CHUNKED_SIZE,
            DATA_CHUNKED_BODY,
            DATA_CHUNKED_BODY_CRLF,
            DATA_CHUNKED_TRAILERS,
            DATA_UNTIL_EOF,
            COMPLETE,
            FAILED,
        };

        struct ParserContext {
            STATE state{STATE::STATUS_LINE};
            std::string buffer{};
            std::size_t cursor{0};
            std::size_t body_read_size{0};
            std::size_t body_bytes_read{0};
            std::size_t chunk_bytes_read{0};
        };

        ResponseParser() = default;
        ~ResponseParser() = default;

        static std::expected<Response, ParseError> parse(std::string_view raw_response);

        std::expected<void, ParseError> parse(Response &response, std::string_view::const_iterator &begin,
                                              const std::string_view::const_iterator end);

        std::expected<void, ParseError> finalize(Response &response);

        ParserContext &getContext();

    private:
        ParserContext context_{};
    };
}// namespace usub::unet::http::v1
