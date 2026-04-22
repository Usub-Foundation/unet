#include "unet/ws/connection.hpp"

#include <utility>

namespace usub::unet::ws {

    Connection::Connection(std::shared_ptr<FrameChannel<ClientFrame>> channel,
                           std::shared_ptr<WriteState> writeState)
        : channel_(std::move(channel)),
          sender_(std::move(writeState)) {}

    usub::uvent::task::Awaitable<std::optional<ClientFrame>> Connection::recv() {
        co_return co_await channel_->recv();
    }

    usub::uvent::task::Awaitable<void> Connection::sendText(std::string_view payload) {
        co_await sender_.sendText(payload);
    }

    usub::uvent::task::Awaitable<void> Connection::sendBinary(std::string_view payload) {
        co_await sender_.sendBinary(payload);
    }

    usub::uvent::task::Awaitable<void> Connection::sendFrame(const ServerFrame &frame) {
        co_await sender_.sendFrame(frame);
    }

    usub::uvent::task::Awaitable<void> Connection::ping(std::string_view payload) {
        co_await sender_.ping(payload);
    }

    usub::uvent::task::Awaitable<void> Connection::pong(std::string_view payload) {
        co_await sender_.pong(payload);
    }

    usub::uvent::task::Awaitable<void> Connection::close(CloseCode code, std::string_view reason) {
        co_await sender_.close(code, reason);
    }

    Sender Connection::sender() const {
        return sender_;
    }

}  // namespace usub::unet::ws
