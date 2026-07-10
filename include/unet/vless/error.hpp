#pragma once

#include <cstdint>
#include <string>

namespace usub::unet::vless {

    struct ParseError {
        enum class CODE : std::uint8_t {
            UNSUPPORTED_PROTOCOL_VERSION,// only version 0 is defined
            UNKNOWN_COMMAND,             // command byte outside {TCP, UDP, MUX}
            UNKNOWN_ADDRESS_TYPE,        // address_type outside {IPv4, Domain, IPv6}
            INVALID_DOMAIN_LENGTH,       // domain length of 0
            FAILED_STATE,                // step() called on a parser already in FAILED
        };
        CODE code{};
        std::string message{};
    };

}// namespace usub::unet::vless
