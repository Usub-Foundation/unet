#pragma once

#include <cstdint>
#include <string>

namespace usub::unet::ws {

    struct FrameParseError {
        enum class CODE : std::uint8_t {
            RESERVED_BITS_SET,       // RSV1/2/3 set without negotiated extension (§5.2)
            CONTROL_FRAME_FRAGMENTED,// §5.4: control frames MUST NOT be fragmented
            CONTROL_FRAME_TOO_LARGE, // §5.5: control payload MUST be ≤125 bytes
            FAILED_STATE,            // step() called on a parser already in FAILED
        };
        CODE code{};
        std::string message{};
    };

}// namespace usub::unet::ws
