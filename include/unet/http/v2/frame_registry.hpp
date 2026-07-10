#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <span>
#include <unordered_map>
#include <utility>

#include <uvent/Uvent.h>

#include "unet/core/transport/transport.hpp"
#include "unet/http/v2/wire/frames.hpp"
#include "unet/http/v2/wire/types.hpp"


namespace usub::unet::http::v2 {

    struct Connection;

    struct FrameContext {
        Connection                                &conn;
        FrameHeader                                header;
        std::span<const std::byte>                 payload;
        usub::unet::core::transport::Transport    &transport;
        std::function<void(std::uint8_t /*type*/, std::uint8_t /*flags*/,
                           std::uint32_t /*stream_id*/,
                           std::span<const std::byte> /*payload*/)> send_frame;
    };

    using FrameHandler = usub::uvent::task::Awaitable<std::expected<void, ERROR_CODE>> (*)(FrameContext);

    struct FrameRegistry {
        std::unordered_map<std::uint8_t, FrameHandler> handlers{};

        void registerHandler(std::uint8_t id, FrameHandler fn) { handlers[id] = fn; }
        void registerHandler(FRAME_TYPE id, FrameHandler fn) { registerHandler(std::to_underlying(id), fn); }

        FrameHandler find(std::uint8_t id) const noexcept {
            auto it = handlers.find(id);
            return it == handlers.end() ? nullptr : it->second;
        }
    };

}// namespace usub::unet::http::v2
