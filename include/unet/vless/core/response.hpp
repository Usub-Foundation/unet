#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace usub::unet::vless {
    struct ResponseHeader {
        std::uint8_t protocol_version{};
        std::uint8_t addons_length{};
        std::vector<std::byte> addons{};
    };

    struct Response {
        ResponseHeader header{};
        std::vector<std::byte> body{};
    };
}