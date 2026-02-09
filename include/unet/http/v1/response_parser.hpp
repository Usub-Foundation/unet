#pragma once


namespace usub::unet::http::v1 {

    class ResponseParser {
    public:
        enum class STATE : std::uint8_t {
            PROTOCOL,
            STATUS_CODE,
            REASON_PHRASE,
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
            STATE state{STATE::PROTOCOL};
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