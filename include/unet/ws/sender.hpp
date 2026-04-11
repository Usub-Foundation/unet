#pragma once

// ws::Sender — copyable, storable handle for writing to a WebSocket connection.
//
// Obtain one from Connection::sender() and store it anywhere — a registry, a
// broadcast list, another coroutine — to push messages to a specific client
// from any point in the application.
//
// Safety:
//  • expired() returns true once the connection closes; all sends become no-ops.
//  • The transport pointer is nulled by Session::onClose() before the socket
//    tears down, so there is no use-after-free.
//  • Write serialization: the draining flag prevents two concurrent senders
//    interleaving frame bytes in Uvent's cooperative single-threaded model.

#include <memory>
#include <queue>
#include <string>
#include <string_view>

#include <uvent/Uvent.h>

#include "unet/core/streams/stream.hpp"
#include "unet/ws/core/frame.hpp"
#include "unet/ws/wire/server_frame_serializer.hpp"

namespace usub::unet::ws {

    struct WriteState {
        usub::unet::core::stream::Transport *transport{nullptr};
        std::queue<std::string>              pending{};
        bool                                 draining{false};
    };

    class Sender {
    public:
        Sender() = default;
        explicit Sender(std::shared_ptr<WriteState> state) : state_(std::move(state)) {}

        [[nodiscard]] bool expired() const noexcept {
            return !state_ || !state_->transport;
        }

        usub::uvent::task::Awaitable<void> send_text(std::string_view payload) {
            co_await enqueue_and_drain(ServerFrameSerializer::text(payload));
        }

        usub::uvent::task::Awaitable<void> send_binary(std::string_view payload) {
            co_await enqueue_and_drain(ServerFrameSerializer::binary(payload));
        }

        usub::uvent::task::Awaitable<void> send_frame(const ServerFrame &frame) {
            co_await enqueue_and_drain(ServerFrameSerializer::serialize(frame));
        }

        usub::uvent::task::Awaitable<void> ping(std::string_view payload = {}) {
            co_await enqueue_and_drain(ServerFrameSerializer::ping(payload));
        }

        usub::uvent::task::Awaitable<void> pong(std::string_view payload = {}) {
            co_await enqueue_and_drain(ServerFrameSerializer::pong(payload));
        }

        usub::uvent::task::Awaitable<void> close(CloseCode code = CloseCode::NORMAL,
                                                  std::string_view reason = {}) {
            co_await enqueue_and_drain(ServerFrameSerializer::close(code, reason));
        }

    private:
        usub::uvent::task::Awaitable<void> enqueue_and_drain(std::string bytes) {
            if (!state_ || !state_->transport) co_return;

            state_->pending.push(std::move(bytes));

            if (state_->draining) co_return;

            state_->draining = true;
            while (!state_->pending.empty()) {
                if (!state_->transport) break;
                auto chunk = std::move(state_->pending.front());
                state_->pending.pop();
                co_await state_->transport->send(chunk);
            }
            state_->draining = false;
        }

        std::shared_ptr<WriteState> state_;
    };

}  // namespace usub::unet::ws
