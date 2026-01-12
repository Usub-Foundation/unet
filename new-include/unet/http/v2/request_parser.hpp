#pragma once

#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "unet/http/error.hpp"
#include "unet/http/request.hpp"
#include "unet/http/v2/frames.hpp"
#include "unet/http/v2/hpack.hpp"
#include "unet/utils/string_utils.h"

namespace usub::unet::http::v2 {

    class RequestParser {
    public:
        enum class STATE : std::uint8_t {
            IDLE,
            HEADERS_START,
            HEADERS_CONTINUATION,
            HEADERS_DONE,
            DATA_FRAME,
            DATA_CHUNK_DONE,
            TRAILERS_START,
            TRAILERS_CONTINUATION,
            TRAILERS_DONE,
            COMPLETE,
            FAILED,
        };

        struct ParserContext {
            STATE state_{STATE::IDLE};

            // std::vector<std::uint8_t> buffer{}; Moved to ServerSession

            std::size_t headers_size{0};
            std::size_t body_bytes_read{0};
        };

        RequestParser() = default;
        ~RequestParser() = default;

        // Tries to parse full request
        // static std::expected<Request, ParseError> parse(const std::string_view raw_request);

        std::expected<void, ParseError> parse(Request &request, const FrameHeader &frame_header,
                                              std::string_view::const_iterator &begin,
                                              const std::string_view::const_iterator end);

        ParserContext &getContext();

    private:
        ParserContext context_{};
    };
}// namespace usub::unet::http::v2
