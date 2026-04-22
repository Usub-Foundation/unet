#pragma once

// WebSocket opening handshake utilities (RFC 6455 section 4).

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace usub::unet::ws {

    inline constexpr std::string_view MAGIC = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    [[nodiscard]] std::array<uint8_t, 20> sha1(std::string_view data);

    [[nodiscard]] std::string base64Encode(std::span<const uint8_t> data);

    [[nodiscard]] std::string makeAcceptKey(std::string_view clientKey);

    [[nodiscard]] bool validateRequest(std::string_view upgradeHeader,
                                       std::string_view connectionHeader,
                                       std::string_view versionHeader);

}  // namespace usub::unet::ws
