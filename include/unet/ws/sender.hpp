#pragma once

// ws::Sender - copyable, storable handle for writing to a WebSocket connection.

#include <memory>
#include <queue>
#include <string>
#include <string_view>

#include <uvent/Uvent.h>

#include "unet/core/streams/stream.hpp"
#include "unet/ws/core/frame.hpp"

namespace usub::unet::ws {

    struct WriteState {
        usub::unet::core::stream::Transport *transport{nullptr};
        std::queue<std::string> pending{};
        bool draining{false};
    };

    class Sender {
    public:
        Sender() = default;
        explicit Sender(std::shared_ptr<WriteState> state);

        [[nodiscard]] bool expired() const noexcept;

        usub::uvent::task::Awaitable<void> sendText(std::string_view payload);

        usub::uvent::task::Awaitable<void> sendBinary(std::string_view payload);

        usub::uvent::task::Awaitable<void> sendFrame(const ServerFrame &frame);

        usub::uvent::task::Awaitable<void> ping(std::string_view payload = {});

        usub::uvent::task::Awaitable<void> pong(std::string_view payload = {});

        usub::uvent::task::Awaitable<void> close(CloseCode code = CloseCode::NORMAL,
                                                 std::string_view reason = {});

    private:
        usub::uvent::task::Awaitable<void> enqueueAndDrain(std::string bytes);

        std::shared_ptr<WriteState> state_;
    };

}  // namespace usub::unet::ws
