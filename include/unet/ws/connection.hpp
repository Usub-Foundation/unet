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
                   std::shared_ptr<WriteState> writeState);

        usub::uvent::task::Awaitable<std::optional<ClientFrame>> recv();

        usub::uvent::task::Awaitable<void> sendText(std::string_view payload);

        usub::uvent::task::Awaitable<void> sendBinary(std::string_view payload);

        usub::uvent::task::Awaitable<void> sendFrame(const ServerFrame &frame);

        usub::uvent::task::Awaitable<void> ping(std::string_view payload = {});

        usub::uvent::task::Awaitable<void> pong(std::string_view payload = {});

        usub::uvent::task::Awaitable<void> close(CloseCode code = CloseCode::NORMAL,
                                                 std::string_view reason = {});

        [[nodiscard]] Sender sender() const;

    private:
        std::shared_ptr<FrameChannel<ClientFrame>> channel_;
        Sender sender_;
    };

}  // namespace usub::unet::ws
