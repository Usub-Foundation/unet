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
            STATUS_VERSION,  // parse "HTTP/1.1" token until SP
            STATUS_CODE,     // parse 3 digits until SP
            STATUS_REASON,   // parse reason phrase until CR
            STATUS_LINE_CRLF,// expect '\n' after '\r',// parses entire: HTTP/x.y SP ddd SP reason CRLF (in one state)

            HEADER_KEY,        // field-name (tchar+), stops on ':'
            HEADER_VALUE,      // optional OWS + field-value until CR
            HEADER_CR,         // expects '\n' (you already do CR->LF split in request parser)
            HEADER_LF,         // commit header, decide next: next header or end-of-headers
            HEADERS_CRLF,      // consumes final '\n' after the empty-line '\r'
            HEADERS_VALIDATION,// compute body framing mode, validate TE/CL rules, status-code rules
            HEADERS_DONE,      // middleware hook like your request parser (optional but consistent)

            DATA_CONTENT_LENGTH,   // read exactly Content-Length bytes (0 => COMPLETE)
            DATA_CHUNKED_SIZE,     // read hex digits until '\r' (optionally ignore extensions)
            DATA_CHUNKED_SIZE_CRLF,// expects '\n', computes chunk size, chooses DATA or LAST
            DATA_CHUNKED_DATA,     // read exactly chunk_size bytes
            DATA_CHUNKED_DATA_CR,  // expect '\r'
            DATA_CHUNKED_DATA_LF,  // expect '\n'
            DATA_CHUNK_DONE,       // reset counters and go back to DATA_CHUNKED_SIZE
            DATA_CHUNKED_LAST_CR,  // expect '\r' after 0-size line (or after size CRLF => last)
            DATA_CHUNKED_LAST_LF,  // expect '\n', then trailers or done
            DATA_DONE,             // end-of-message check (trailers unsupported or validated)

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
