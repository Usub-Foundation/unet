#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

#include "unet/ws/core/frame.hpp"
#include "unet/ws/error.hpp"

namespace usub::unet::ws {

    class FrameParser {
    public:
        enum class STATE : std::uint8_t {
            BYTE0,     // FIN | RSV1 | RSV2 | RSV3 | opcode(4)
            BYTE1,     // MASK | payload-length(7)
            EXT_LEN_16,// 2 bytes when payload_length == 126
            EXT_LEN_64,// 8 bytes when payload_length == 127
            MASK_KEY,  // 4 bytes when mask == true
            PAYLOAD,   // header complete; payload streamed externally
            FAILED,
        };

        struct ParserContext {
            STATE state{STATE::BYTE0};
            std::uint64_t payload_len{0};     // resolved length (after extended decode, if any)
            std::uint64_t payload_consumed{0};// bytes the caller has reported as processed

            std::uint8_t bytes_in_field{0};
        };

        FrameParser() = default;

        [[nodiscard]] ParserContext &getContext() noexcept { return context_; }
        [[nodiscard]] const ParserContext &getContext() const noexcept { return context_; }

        void reset() noexcept { context_ = {}; }


        [[nodiscard]] std::expected<void, FrameParseError> stepHeader(FrameHeader &header,
                                                                      std::string_view::const_iterator &begin,
                                                                      const std::string_view::const_iterator end);

        bool advancePayload(std::uint64_t n) noexcept {
            context_.payload_consumed += n;
            return context_.payload_consumed >= context_.payload_len;
        }

        [[nodiscard]] std::uint64_t payloadLength() const noexcept { return context_.payload_len; }
        [[nodiscard]] std::uint64_t payloadConsumed() const noexcept { return context_.payload_consumed; }
        [[nodiscard]] std::uint64_t payloadRemaining() const noexcept {
            return context_.payload_len > context_.payload_consumed ? context_.payload_len - context_.payload_consumed
                                                                    : 0;
        }
        [[nodiscard]] bool frameDone() const noexcept { return context_.payload_consumed >= context_.payload_len; }

    private:
        ParserContext context_{};
    };

}// namespace usub::unet::ws
