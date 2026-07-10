#include "unet/ws/writer.hpp"

#include <utility>

#include "unet/ws/wire/frame_serializer.hpp"

namespace usub::unet::ws {

    ServerWriter::ServerWriter(std::shared_ptr<WriteState> state)
        : state_(std::move(state)) {}

    bool ServerWriter::expired() const noexcept {
        if (!this->state_) return true;
        std::scoped_lock lk(this->state_->mu);
        return !this->state_->transport;
    }

    usub::uvent::task::Awaitable<void> ServerWriter::sendText(std::string_view payload) {
        co_await enqueueAndDrain(FrameSerializer::text(payload));
    }

    usub::uvent::task::Awaitable<void> ServerWriter::sendBinary(std::string_view payload) {
        co_await enqueueAndDrain(FrameSerializer::binary(payload));
    }

    usub::uvent::task::Awaitable<void> ServerWriter::sendFrame(const Frame &frame) {
        co_await enqueueAndDrain(FrameSerializer::serialize(frame));
    }

    usub::uvent::task::Awaitable<void> ServerWriter::ping(std::string_view payload) {
        co_await enqueueAndDrain(FrameSerializer::ping(payload));
    }

    usub::uvent::task::Awaitable<void> ServerWriter::pong(std::string_view payload) {
        co_await enqueueAndDrain(FrameSerializer::pong(payload));
    }

    usub::uvent::task::Awaitable<void> ServerWriter::close(CLOSE_CODE code, std::string_view reason) {
        co_await enqueueAndDrain(FrameSerializer::close(code, reason));
    }

    usub::uvent::task::Awaitable<void> ServerWriter::enqueueAndDrain(std::string bytes) {
        if (!this->state_) co_return;

        bool own_drain = false;
        {
            std::scoped_lock lk(this->state_->mu);
            if (!this->state_->transport) co_return;
            this->state_->pending.push(std::move(bytes));
            if (!this->state_->draining) {
                this->state_->draining = true;
                own_drain              = true;
            }
        }
        if (!own_drain) co_return;

        for (;;) {
            std::string                                              chunk;
            std::shared_ptr<usub::unet::core::transport::Transport>  tr;
            {
                std::scoped_lock lk(this->state_->mu);
                if (this->state_->pending.empty() || !this->state_->transport) {
                    this->state_->draining = false;
                    co_return;
                }
                chunk = std::move(this->state_->pending.front());
                this->state_->pending.pop();
                tr = this->state_->transport;
            }
            co_await tr->send(chunk);
        }
    }

}// namespace usub::unet::ws
