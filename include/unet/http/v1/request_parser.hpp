#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <expected>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "unet/http/error.hpp"
#include "unet/http/request.hpp"

namespace usub::unet::http::v1 {

    class RequestParser {
    public:
        enum class STATE : std::uint8_t {
            METHOD_TOKEN,
            URI,
            ORIGIN_FORM,
            ORIGIN_PATH,
            ORIGIN_QUERY,
            ORIGIN_FRAGMENT,
            ABSOLUTE_FORM,
            AUTHORITY_FORM,
            ASTERISK_FORM,
            VERSION,
            REQUEST_LINE_CRLF_HTTP_0_9,// Not implemented
            REQUEST_LINE_CRLF,
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
            DATA_CHUNKED_LAST_CR,
            DATA_CHUNKED_LAST_LF,
            DATA_CHUNK_DONE,
            DATA_DONE,
            TRAILER_KEY,
            TRAILER_VALUE,
            TRAILER_CR,
            TRAILER_LF,
            TRAILERS_DONE,
            COMPLETE,
            FAILED// ERROR STATE, can't name it ERROR because of conflict with ERROR macro on Windows, my kindest regards to windows devs.
        };

        struct ParserContext {
            STATE state{STATE::METHOD_TOKEN};
            STATE post_header_middleware_state{
                    STATE::METHOD_TOKEN};// State after we invoke middleware, to avoid reparse
            std::pair<std::string, std::string> kv_buffer{};
            std::size_t headers_size{0};

            std::size_t body_bytes_read{0};
            // std::size_t chunk_bytes_read{0};

            std::size_t current_state_size{0};

            std::size_t body_read_size{};// content-length or current chunk read size
        };

    public:
        RequestParser() = default;
        ~RequestParser() = default;

        // Tries to parse full request
        static std::expected<Request, ParseError> parse(const std::string_view raw_request);

        std::expected<void, ParseError> parse(Request &request, std::string_view::const_iterator &begin,
                                              const std::string_view::const_iterator end);

        ParserContext &getContext();

    private:
        ParserContext context_;
    };
}// namespace usub::unet::http::v1
