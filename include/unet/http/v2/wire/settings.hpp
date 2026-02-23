#pragma once

#include <cstdint>

namespace usub::unet::http::v2 {
    enum class SETTINGS : std::uint16_t {
        HEADER_TABLE_SIZE = 0x01,
        ENABLE_PUSH = 0x02,
        MAX_CONCURRENT_STREAMS = 0x03,
        INITIAL_WINDOW_SIZE = 0x04,
        MAX_FRAME_SIZE = 0x05,
        MAX_HEADER_LIST_SIZE = 0x06,
    };
}