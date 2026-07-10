#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace usub::unet::vless {

    struct Domain {
        std::uint8_t length{};
        std::string value{};
    };

    // IPV4 (0x01), IPV6 (0x03), DOMAIN (0x02)
    using Address = std::variant<std::array<std::byte, 4>,// IPV4
                                 Domain,                  // DOMAIN
                                 std::array<std::byte, 16>// IPV6
                                 >;

    struct Request {
        std::uint8_t protocol_version{};
        std::array<std::byte, 16> uuid{};
        std::uint8_t addons_length{};
        std::vector<std::byte> addons{};
        std::uint8_t command{};
        std::uint16_t destination_port{};// host byte order in-memory
        std::uint8_t address_type{};
        Address address{};
    };

}// namespace usub::unet::vless
