#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace usub::unet::vless {
    struct Response {
        std::uint8_t protocol_version{};
        std::uint8_t addons_length{};
        std::vector<std::byte> addons{};
    };

}// namespace usub::unet::vless