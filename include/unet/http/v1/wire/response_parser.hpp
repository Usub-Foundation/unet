#pragma once
#include <array>
#include <charconv>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <string>
#include <string_view>


#include "unet/http/core/response.hpp"
#include "unet/http/error.hpp"

namespace usub::unet::http::v1 {

    class ResponseParser {
    public:
        enum class STATE : std::uint8_t {
            STATUS_VERSION,
            STATUS_CODE,
            STATUS_REASON,
            STATUS_LINE_CRLF,

            HEADER_KEY,
            HEADER_VALUE,
            HEADER_CR,
            HEADER_LF,
            HEADERS_CRLF,
            HEADERS_VALIDATION,
            HEADERS_DONE,

            DATA_CONTENT_LENGTH,
            DATA_CHUNKED_SIZE,
            DATA_CHUNKED_SIZE_CRLF,
            DATA_CHUNKED_DATA,
            DATA_CHUNKED_DATA_CR,
            DATA_CHUNKED_DATA_LF,
            DATA_CHUNK_DONE,
            DATA_CHUNKED_LAST_CR,
            DATA_CHUNKED_LAST_LF,
            DATA_DONE,

            TRAILER_KEY,
            TRAILER_VALUE,
            TRAILER_CR,
            TRAILER_LF,
            TRAILERS_DONE,

            BODY_UNTIL_CLOSE,

            COMPLETE,
            FAILED
        };

        enum class AfterHeaders : std::uint8_t { COMPLETE, CHUNKED, CONTENT_LENGTH, UNTIL_CLOSE };
        struct ParserContext {
            STATE state{STATE::STATUS_VERSION};

            AfterHeaders after_headers{AfterHeaders::COMPLETE};


            std::pair<std::string, std::string> kv_buffer{};
            std::size_t current_state_size{0};
            std::size_t headers_size{0};


            std::size_t body_bytes_read{0};
            std::size_t body_read_size{};
        };
        ResponseParser() = default;
        ~ResponseParser() = default;

        static std::expected<Response, ParseError> parse(const std::string_view raw_response);

        std::expected<void, ParseError> parse(Response &response, std::string_view::const_iterator &begin,
                                              const std::string_view::const_iterator end);

        ParserContext &getContext();

    private:
        ParserContext context_;
    };
}// namespace usub::unet::http::v1
