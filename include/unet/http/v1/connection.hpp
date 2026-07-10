#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "unet/http/peer_info.hpp"


namespace usub::unet::http::v1 {

    struct Connection {
        PeerInfo peer{};

        std::chrono::milliseconds header_timeout{std::chrono::milliseconds{20000}};
        std::chrono::milliseconds idle_body_timeout{std::chrono::milliseconds{20000}};

        bool keep_alive_enabled{true};
        bool honour_keep_alive{true};
        std::chrono::milliseconds default_keep_alive_timeout{std::chrono::milliseconds{20000}};
        std::chrono::milliseconds max_keep_alive_timeout{std::chrono::milliseconds{20000}};
        std::uint32_t keep_alive_max_requests{0};
        std::uint32_t requests_served{0};
        bool close_after_cycle{false};
    };

}// namespace usub::unet::http::v1
