#pragma once

#include <cstdint>


namespace usub::unet::http::v2 {

    enum class FLAGS : std::uint8_t {
        END_STREAM  = 0x01,
        ACK         = 0x01,
        END_HEADERS = 0x04,
        PADDED      = 0x08,
        PRIORITY    = 0x20,
    };

    constexpr std::uint8_t operator&(std::uint8_t lhs, FLAGS rhs) noexcept {
        return lhs & static_cast<std::uint8_t>(rhs);
    }
    constexpr std::uint8_t operator|(std::uint8_t lhs, FLAGS rhs) noexcept {
        return lhs | static_cast<std::uint8_t>(rhs);
    }
    constexpr std::uint8_t &operator|=(std::uint8_t &lhs, FLAGS rhs) noexcept {
        lhs = lhs | rhs;
        return lhs;
    }

}// namespace usub::unet::http::v2
