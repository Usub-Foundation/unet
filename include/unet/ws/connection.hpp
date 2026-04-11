#pragma once

#include <memory>
#include <optional>
#include <string_view>

#include <uvent/Uvent.h>

#include "unet/ws/channel.hpp"
#include "unet/ws/core/frame.hpp"
#include "unet/ws/sender.hpp"

namespace usub::unet::ws {

    class Connection {
    public:
        Connection(std::shared_ptr<FrameChannel<ClientFrame>> channel,
                   std::shared_ptr<WriteState>                write_state)
            : channel_(std::move(channel)),
              sender_(std::move(write_state)) {}

        // ── Receiving ──────────────────────────────────────────────────────────

        // Suspend until the next frame arrives from the peer.
        // Returns nullopt when the connection has closed — loop exit condition:
        //
        //   while (auto frame = co_await conn.recv()) { … }
        usub::uvent::task::Awaitable<std::optional<ClientFrame>> recv() {
            co_return co_await channel_->recv();
        }

        // ── Sending ────────────────────────────────────────────────────────────

        usub::uvent::task::Awaitable<void> send_text(std::string_view payload) {
            co_await sender_.send_text(payload);
        }

        usub::uvent::task::Awaitable<void> send_binary(std::string_view payload) {
            co_await sender_.send_binary(payload);
        }

        usub::uvent::task::Awaitable<void> send_frame(const ServerFrame &frame) {
            co_await sender_.send_frame(frame);
        }

        usub::uvent::task::Awaitable<void> ping(std::string_view payload = {}) {
            co_await sender_.ping(payload);
        }

        usub::uvent::task::Awaitable<void> pong(std::string_view payload = {}) {
            co_await sender_.pong(payload);
        }

        usub::uvent::task::Awaitable<void> close(CloseCode code = CloseCode::NORMAL,
                                                  std::string_view reason = {}) {
            co_await sender_.close(code, reason);
        }

        // ── Sharing ────────────────────────────────────────────────────────────

        // Returns a copyable Sender tied to this connection.
        // Store it in a registry or broadcast list to push to this client
        // from anywhere in the application.
        [[nodiscard]] Sender sender() const { return sender_; }

    private:
        std::shared_ptr<FrameChannel<ClientFrame>> channel_;
        Sender                                     sender_;
    };

}  // namespace usub::unet::ws
