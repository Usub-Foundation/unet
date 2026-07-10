#pragma once

#include <cstdint>


namespace usub::unet::http::v2 {

    struct FlowControl {
        std::int32_t recv_window{65535};
        std::int32_t send_window{65535};

        constexpr bool applySendWindowUpdate(std::uint32_t increment) noexcept {
            std::int64_t v = static_cast<std::int64_t>(send_window) + static_cast<std::int64_t>(increment);
            if (v > 0x7fffffff) return false;
            send_window = static_cast<std::int32_t>(v);
            return true;
        }

        constexpr bool applyRecvWindowUpdate(std::uint32_t increment) noexcept {
            std::int64_t v = static_cast<std::int64_t>(recv_window) + static_cast<std::int64_t>(increment);
            if (v > 0x7fffffff) return false;
            recv_window = static_cast<std::int32_t>(v);
            return true;
        }

        constexpr bool consumeSendCredit(std::uint32_t n) noexcept {
            if (static_cast<std::int64_t>(send_window) < static_cast<std::int64_t>(n)) return false;
            send_window -= static_cast<std::int32_t>(n);
            return true;
        }

        constexpr void consumeRecvCredit(std::uint32_t n) noexcept { recv_window -= static_cast<std::int32_t>(n); }
    };

}// namespace usub::unet::http::v2
