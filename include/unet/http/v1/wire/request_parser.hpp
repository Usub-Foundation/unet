#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "unet/http/core/char_table.hpp"
#include "unet/http/core/request.hpp"
#include "unet/http/error.hpp"

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
            ABSOLUTE_FORM_AFTER_SCHEME,
            ABSOLUTE_FORM_AFTER_FIRST_SLASH,
            ABSOLUTE_FORM_HOST,
            ABSOLUTE_FORM_HOST_IPV6,
            ABSOLUTE_FORM_AFTER_HOST,
            ABSOLUTE_FORM_PORT,
            AUTHORITY_FORM,
            AUTHORITY_FORM_HOST_IPV6,
            AUTHORITY_FORM_AFTER_HOST,
            AUTHORITY_FORM_PORT,
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
            HEADERS_DONE_CL,
            HEADERS_DONE_CHUNKED,
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
            FAILED// ERROR STATE, can't name it ERROR because of conflict with ERROR macro on Windows. MAY Be renamed to ERROR later
            // Since the http2 path has ERROR State as the rfc defined name hence here it's the same
        };

        enum class BODY_TYPE : std::uint8_t { NONE, CONTENT_LENGTH, CHUNKED };

        struct ParserContext {
            STATE state{STATE::METHOD_TOKEN};
            BODY_TYPE body_type{BODY_TYPE::NONE};

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

        std::expected<void, ParseError> step(RequestReader &reader, std::string_view::const_iterator &begin,
                                             const std::string_view::const_iterator end);

        std::string takePendingBody() noexcept {
            std::string out;
            out.swap(pending_body_);
            return out;
        }

        ParserContext &getContext();

    private:
        ParserContext context_;
        std::string pending_body_{};
    };
}// namespace usub::unet::http::v1
