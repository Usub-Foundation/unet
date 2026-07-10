#pragma once

#include <cstdint>

namespace usub::unet::vless
{
    enum class VERSION : std::uint8_t {
        V0 = 0x00,
    };

    enum class COMMAND : std::uint8_t {
        TCP = 0x01,
        UDP = 0x02,
        MUX = 0x03,
    };

    enum class ADDRESS_TYPE : std::uint8_t {
        IPV4        = 0x01,
        DOMAIN_ADDR = 0x02,// DOMAIN clashes with a Windows/MSVC macro (math.h: #define DOMAIN _DOMAIN)
        IPV6        = 0x03,
    };

} // namespace usub::unet::vless
