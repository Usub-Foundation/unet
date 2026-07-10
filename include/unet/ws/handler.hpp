#pragma once

#include <cstdint>

#include "unet/core/io_provider.hpp"
#include "unet/ws/core/frame.hpp"
#include "unet/ws/reader.hpp"
#include "unet/ws/writer.hpp"

namespace usub::unet::ws {

    struct Handler {
        Awaitable<void> text(ClientReader &reader, ServerWriter writer);
        Awaitable<void> binary(ClientReader &reader, ServerWriter writer);
        Awaitable<void> reserved3(ClientReader &reader, ServerWriter writer);
        Awaitable<void> reserved4(ClientReader &reader, ServerWriter writer);
        Awaitable<void> reserved5(ClientReader &reader, ServerWriter writer);
        Awaitable<void> reserved6(ClientReader &reader, ServerWriter writer);
        Awaitable<void> reserved7(ClientReader &reader, ServerWriter writer);

        Awaitable<void> close(ServerWriter writer, const Frame &frame);
        Awaitable<void> ping(ServerWriter writer, const Frame &frame);
        Awaitable<void> pong(ServerWriter writer, const Frame &frame);
        Awaitable<void> reservedB(ServerWriter writer, const Frame &frame);
        Awaitable<void> reservedC(ServerWriter writer, const Frame &frame);
        Awaitable<void> reservedD(ServerWriter writer, const Frame &frame);
        Awaitable<void> reservedE(ServerWriter writer, const Frame &frame);
        Awaitable<void> reservedF(ServerWriter writer, const Frame &frame);
        Awaitable<void> onOpen(ServerWriter writer);
        Awaitable<void> onClose(ServerWriter writer);

        // Inactivity timeout
        std::uint64_t timeout_ms{20000};
    };

}// namespace usub::unet::ws
