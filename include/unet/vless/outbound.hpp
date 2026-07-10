#pragma once

#include <cstdint>
#include <memory>

#include <uvent/tasks/Awaitable.h>

#include "unet/core/transport/transport.hpp"
#include "unet/vless/core/request.hpp"

namespace usub::unet::vless {

    // Test-only: opens a plaintext outbound TCP connection to the destination described by
    // `address` + `port`. Returns nullptr on failure. Not meant for production —
    // no DNS caching, no policy, no source-IP selection.
    usub::uvent::task::Awaitable<std::unique_ptr<usub::unet::core::transport::Transport>>
    dialTcp(const Address &address, std::uint16_t port);

}// namespace usub::unet::vless
