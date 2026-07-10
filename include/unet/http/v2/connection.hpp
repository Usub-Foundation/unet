#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "unet/http/peer_info.hpp"
#include "unet/http/v2/wire/settings.hpp"


namespace usub::unet::http::v2 {

    struct Connection {
        PeerInfo peer{};

        std::chrono::milliseconds idle_header_timeout{20000};
        std::chrono::milliseconds idle_timeout{20000};

        // RFC 9113 §6.5.3 — 0 disables.
        std::chrono::milliseconds settings_ack_timeout{5000};

        // RFC 9113 §10.5.1.
        std::uint32_t max_continuations_per_header_block{16};

        Settings initial_local_settings{
                .header_table_size       = 4096,
                .enable_push             = 0,
                .max_concurrent_streams  = 100,
                .initial_window_size     = 65535,
                .max_frame_size          = 16384,
                .max_header_list_size    = 8192,
                .enable_connect_protocol = 1,
                .custom                  = {},
        };
    };

}// namespace usub::unet::http::v2
