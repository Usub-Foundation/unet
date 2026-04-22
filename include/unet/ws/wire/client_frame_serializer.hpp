#pragma once

// ClientFrameSerializer - serializes client->server frames; always masks.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "unet/ws/core/frame.hpp"

namespace usub::unet::ws {

    class ClientFrameSerializer {
    public:
        [[nodiscard]] static std::string serialize(const ClientFrame &frame);

        [[nodiscard]] static std::string text(std::string_view payload,
                                              std::array<uint8_t, 4> mask_key,
                                              bool fin = true);

        [[nodiscard]] static std::string binary(std::string_view payload,
                                                std::array<uint8_t, 4> mask_key,
                                                bool fin = true);

        [[nodiscard]] static std::string ping(std::string_view payload,
                                              std::array<uint8_t, 4> mask_key);

        [[nodiscard]] static std::string pong(std::string_view payload,
                                              std::array<uint8_t, 4> mask_key);

        [[nodiscard]] static std::string close(CloseCode code,
                                               std::array<uint8_t, 4> mask_key,
                                               std::string_view reason = {});
    };

}  // namespace usub::unet::ws
