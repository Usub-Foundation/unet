#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <vector>

#include "unet/http/core/request.hpp"
#include "unet/http/v2/wire/hpack.hpp"
#include "unet/http/v2/wire/types.hpp"


namespace usub::unet::http::v2 {

    class RequestParser {
    public:
        enum class STATE : std::uint8_t {
            HEADERS,
            HEADERS_DONE,
            TRAILERS,
            DONE,
            FAILED,
        };

        struct ParserContext {
            STATE state{STATE::HEADERS};
            bool  end_stream_pending{false};

            std::vector<std::byte> header_acc{};

            // RFC 9113 §8.1.2.6 — declared content-length must equal the summed DATA payloads.
            std::optional<std::uint64_t> expected_body_bytes{};
            std::uint64_t                body_bytes_received{0};
        };

        RequestParser()  = default;
        ~RequestParser() = default;

        ParserContext       &getContext() noexcept { return context_; }
        const ParserContext &getContext() const noexcept { return context_; }

        bool isDone() const noexcept { return context_.state == STATE::DONE; }

        // Auto-transitions HEADERS_DONE → TRAILERS on next call; callers don't signal it.
        void appendHeaderFragment(std::span<const std::byte> fragment) {
            if (context_.state == STATE::HEADERS_DONE) context_.state = STATE::TRAILERS;
            context_.header_acc.insert(context_.header_acc.end(), fragment.begin(), fragment.end());
        }

        // Returns tunnel flag (CONNECT).
        std::expected<bool, ERROR_CODE>
        decodeHeaders(HpackDecoder &decoder, usub::unet::http::RequestReader &out);

        std::expected<void, ERROR_CODE>
        decodeTrailers(HpackDecoder &decoder, usub::unet::http::RequestReader &out);

        void markEndStream() noexcept {
            context_.end_stream_pending = true;
            if (context_.state == STATE::HEADERS_DONE) context_.state = STATE::DONE;
        }

        void markComplete() noexcept {
            context_.end_stream_pending = true;
            context_.state              = STATE::DONE;
        }

    private:
        ParserContext context_{};
    };

}// namespace usub::unet::http::v2
