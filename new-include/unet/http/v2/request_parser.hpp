#pragma once

#include <cstdint>

namespace usub::unet::http::v2 {
    class RequestParser {
    public:
        enum class STATE : std::uint8_t {
            HEADERS,
            HEADERS_DONE,
            DATA,
            DATA_CHUNK_DONE,
            TRAILERS,
            DONE,
            FAILED,
        };

        struct ParserContext {
            STATE state_{STATE::HEADERS};
        };

        RequestParser() = default;
        ~RequestParser() = default;

        ParserContext &getContext();

    private:
        ParserContext context_;
    };
}// namespace usub::unet::http::v2