#pragma once

// FrameChannel<T> — single-producer / single-consumer awaitable queue.
//
// The session (producer) pushes complete frames; the user handler coroutine
// (consumer) co_awaits recv().  Works correctly in Uvent's cooperative model:
// only one coroutine runs at a time, so push() calling h.resume() directly is safe.
//
// Typical instantiations:
//   FrameChannel<ClientFrame>  — server-side (receives masked client frames)
//   FrameChannel<ServerFrame>  — client-side (receives unmasked server frames)

#include <coroutine>
#include <optional>
#include <queue>
#include <utility>

namespace usub::unet::ws {

    template<typename FrameT>
    class FrameChannel {
    public:
        struct Awaiter {
            FrameChannel &ch;

            bool await_ready() noexcept {
                return !ch.queue_.empty() || ch.closed_;
            }

            void await_suspend(std::coroutine_handle<> h) noexcept {
                ch.waiter_ = h;
            }

            std::optional<FrameT> await_resume() noexcept {
                if (ch.queue_.empty()) { return std::nullopt; }
                auto f = std::move(ch.queue_.front());
                ch.queue_.pop();
                return f;
            }
        };

        Awaiter recv() noexcept { return {*this}; }

        void push(FrameT frame) {
            queue_.push(std::move(frame));
            resume_waiter();
        }

        void close() noexcept {
            closed_ = true;
            resume_waiter();
        }

        [[nodiscard]] bool closed() const noexcept { return closed_; }

    private:
        void resume_waiter() noexcept {
            if (waiter_) {
                auto h = std::exchange(waiter_, {});
                h.resume();
            }
        }

        std::queue<FrameT>       queue_{};
        std::coroutine_handle<>  waiter_{};
        bool                     closed_{false};
    };

}  // namespace usub::unet::ws
