#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "unet/ws/core/frame.hpp"

namespace usub::unet::ws {

    class FrameSerializer {
    public:
        [[nodiscard]] static std::string serialize(const Frame &frame);

        [[nodiscard]] static std::string text(std::string_view payload, bool fin = true);
        [[nodiscard]] static std::string binary(std::string_view payload, bool fin = true);

        [[nodiscard]] static std::string reserved3(std::string_view payload, bool fin = true);
        [[nodiscard]] static std::string reserved4(std::string_view payload, bool fin = true);
        [[nodiscard]] static std::string reserved5(std::string_view payload, bool fin = true);
        [[nodiscard]] static std::string reserved6(std::string_view payload, bool fin = true);
        [[nodiscard]] static std::string reserved7(std::string_view payload, bool fin = true);

        [[nodiscard]] static std::string ping(std::string_view payload = {});
        [[nodiscard]] static std::string pong(std::string_view payload = {});
        [[nodiscard]] static std::string close(CLOSE_CODE code = CLOSE_CODE::NORMAL, std::string_view reason = {});

        [[nodiscard]] static std::string reservedB(std::string_view payload = {});
        [[nodiscard]] static std::string reservedC(std::string_view payload = {});
        [[nodiscard]] static std::string reservedD(std::string_view payload = {});
        [[nodiscard]] static std::string reservedE(std::string_view payload = {});
        [[nodiscard]] static std::string reservedF(std::string_view payload = {});
    };

}// namespace usub::unet::ws
