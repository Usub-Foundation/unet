#pragma once


namespace usub::unet::http::v1 {

    class ResponseParser {
    public:
        enum class STATE : std::uint8_t {
            STATUS_LINE,

            HEADER_KEY,
            HEADER_VALUE,
            HEADER_CR,
            HEADERS_CRLF,
            HEADERS_VALIDATION,
            HEADERS_DONE,

            BODY_CONTENT_LENGTH,
            BODY_CHUNKED_SIZE,
            BODY_CHUNKED_SIZE_CRLF,
            BODY_CHUNKED_DATA,
            BODY_CHUNKED_DATA_CR,
            BODY_CHUNKED_DATA_LF,
            BODY_CHUNK_DONE,
            BODY_CHUNKED_LAST_CR,
            BODY_CHUNKED_LAST_LF,
            BODY_DONE,
            TRAILER_KEY,
            TRAILER_VALUE,
            TRAILER_CR,
            TRAILER_LF,
            TRAILERS_DONE,

            BODY_UNTIL_CLOSE,

            COMPLETE,
            FAILED
        };

        struct ParserContext {
            STATE state{STATE::STATUS_LINE};

            std::pair<std::string, std::string> kv_buffer{};
            std::size_t current_state_size{0};


            std::uint8_t status_line_phase{0};
            std::uint8_t status_code_digits{0};
            std::uint16_t status_code_value{0};
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