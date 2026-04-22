#pragma once

// ServerFrameSerializer - serializes server->client frames; never masks.

#include <cstdint>
#include <string>
#include <string_view>

#include "unet/ws/core/frame.hpp"

namespace usub::unet::ws {

    class ServerFrameSerializer {
    public:
        [[nodiscard]] static std::string serialize(const ServerFrame &frame);

        [[nodiscard]] static std::string text(std::string_view payload, bool fin = true);

        [[nodiscard]] static std::string binary(std::string_view payload, bool fin = true);

        [[nodiscard]] static std::string ping(std::string_view payload = {});

        [[nodiscard]] static std::string pong(std::string_view payload = {});

        [[nodiscard]] static std::string close(CloseCode code = CloseCode::NORMAL,
                                               std::string_view reason = {});
    };

}  // namespace usub::unet::ws
