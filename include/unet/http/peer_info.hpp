#pragma once

#include <cstdint>
#include <string>

namespace usub::unet::http {

    struct PeerInfo {
        std::string   ip{};
        std::uint16_t port{0};
        std::string   alpn{};
        bool          ssl{false};
    };

}// namespace usub::unet::http
