#pragma once

// ClientFrameParser - server uses this to parse incoming client->server frames.
// Enforces that every frame carries a mask (RFC 6455 section 5.1).

#include <array>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

#include "unet/ws/core/frame.hpp"

namespace usub::unet::ws {

    struct FrameParseError {
        enum class CODE : uint8_t {
            RESERVED_BITS_SET,
            CONTROL_FRAME_FRAGMENTED,
            CONTROL_FRAME_TOO_LARGE,
            PAYLOAD_TOO_LARGE,
            UNMASKED_CLIENT_FRAME,  // server received unmasked frame - RFC 6455 section 5.1
            MASKED_SERVER_FRAME,    // client received masked frame  - RFC 6455 section 5.1
            FAILED_STATE,
        };
        CODE code{};
        std::string message{};
    };

    class ClientFrameParser {
    public:
        static constexpr std::size_t DEFAULT_MAX_PAYLOAD = 16u * 1024u * 1024u;

        enum class STATE : uint8_t {
            BYTE0,
            BYTE1,
            EXT_LEN,
            MASK_KEY,
            PAYLOAD,
            FAILED,
        };

        struct ParserContext {
            STATE state{STATE::BYTE0};
            uint64_t payload_len{0};

            int ext_len_bytes{0};
            int ext_len_idx{0};
            std::array<uint8_t, 8> ext_len_buf{};

            int mask_idx{0};
        };

        explicit ClientFrameParser(std::size_t maxPayload = DEFAULT_MAX_PAYLOAD);

        [[nodiscard]] ParserContext &getContext() noexcept;
        [[nodiscard]] const ParserContext &getContext() const noexcept;

        void reset() noexcept;

        [[nodiscard]] std::expected<bool, FrameParseError>
        step(ClientFrame &frame,
             std::string_view::const_iterator &begin,
             const std::string_view::const_iterator end);

    private:
        ParserContext ctx_{};
        std::size_t max_payload_{DEFAULT_MAX_PAYLOAD};
    };

}  // namespace usub::unet::ws
