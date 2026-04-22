#include "unet/ws/sender.hpp"

#include <utility>

#include "unet/ws/wire/server_frame_serializer.hpp"

namespace usub::unet::ws {

    Sender::Sender(std::shared_ptr<WriteState> state)
        : state_(std::move(state)) {}

    bool Sender::expired() const noexcept {
        return !state_ || !state_->transport;
    }

    usub::uvent::task::Awaitable<void> Sender::sendText(std::string_view payload) {
        co_await enqueueAndDrain(ServerFrameSerializer::text(payload));
    }

    usub::uvent::task::Awaitable<void> Sender::sendBinary(std::string_view payload) {
        co_await enqueueAndDrain(ServerFrameSerializer::binary(payload));
    }

    usub::uvent::task::Awaitable<void> Sender::sendFrame(const ServerFrame &frame) {
        co_await enqueueAndDrain(ServerFrameSerializer::serialize(frame));
    }

    usub::uvent::task::Awaitable<void> Sender::ping(std::string_view payload) {
        co_await enqueueAndDrain(ServerFrameSerializer::ping(payload));
    }

    usub::uvent::task::Awaitable<void> Sender::pong(std::string_view payload) {
        co_await enqueueAndDrain(ServerFrameSerializer::pong(payload));
    }

    usub::uvent::task::Awaitable<void> Sender::close(CloseCode code, std::string_view reason) {
        co_await enqueueAndDrain(ServerFrameSerializer::close(code, reason));
    }

    usub::uvent::task::Awaitable<void> Sender::enqueueAndDrain(std::string bytes) {
        if (!state_ || !state_->transport) {
            co_return;
        }

        state_->pending.push(std::move(bytes));
        if (state_->draining) {
            co_return;
        }

        state_->draining = true;
        while (!state_->pending.empty()) {
            if (!state_->transport) {
                break;
            }

            auto chunk = std::move(state_->pending.front());
            state_->pending.pop();
            co_await state_->transport->send(chunk);
        }
        state_->draining = false;
    }

}  // namespace usub::unet::ws
