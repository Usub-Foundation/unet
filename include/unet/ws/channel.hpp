#pragma once

// FrameChannel<T> - single-producer / single-consumer awaitable queue.

#include <coroutine>
#include <optional>
#include <queue>
#include <utility>

namespace usub::unet::ws {

    template<typename FrameT>
    class FrameChannel {
    public:
        struct Awaiter {
            FrameChannel &channel;

            bool await_ready() noexcept {
                return !channel.queue_.empty() || channel.closed_;
            }

            void await_suspend(std::coroutine_handle<> handle) noexcept {
                channel.waiter_ = handle;
            }

            std::optional<FrameT> await_resume() noexcept {
                if (channel.queue_.empty()) {
                    return std::nullopt;
                }

                auto frame = std::move(channel.queue_.front());
                channel.queue_.pop();
                return frame;
            }
        };

        Awaiter recv() noexcept { return {*this}; }

        void push(FrameT frame) {
            queue_.push(std::move(frame));
            resumeWaiter();
        }

        void close() noexcept {
            closed_ = true;
            resumeWaiter();
        }

        [[nodiscard]] bool closed() const noexcept { return closed_; }

    private:
        void resumeWaiter() noexcept {
            if (waiter_) {
                auto handle = std::exchange(waiter_, {});
                handle.resume();
            }
        }

        std::queue<FrameT> queue_{};
        std::coroutine_handle<> waiter_{};
        bool closed_{false};
    };

}  // namespace usub::unet::ws
