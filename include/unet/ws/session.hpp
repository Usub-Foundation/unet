#pragma once

#include <functional>
#include <memory>
#include <string_view>

#include <uvent/Uvent.h>

#include "unet/core/streams/stream.hpp"
#include "unet/http/session.hpp"
#include "unet/ws/channel.hpp"
#include "unet/ws/connection.hpp"
#include "unet/ws/core/frame.hpp"
#include "unet/ws/sender.hpp"
#include "unet/ws/wire/client_frame_parser.hpp"

namespace usub::unet::ws {

    using WsHandler = std::function<usub::uvent::task::Awaitable<void>(Connection)>;

    class Session {
    public:
        explicit Session(WsHandler handler);

        usub::uvent::task::Awaitable<usub::unet::http::SessionAction>
        onBytes(std::string_view data, usub::unet::core::stream::Transport &transport);

        usub::uvent::task::Awaitable<void> onClose();

    private:
        WsHandler handler_;
        std::shared_ptr<FrameChannel<ClientFrame>> channel_;
        std::shared_ptr<WriteState> write_state_;
        ClientFrameParser parser_{};
        bool handler_spawned_{false};
    };

}  // namespace usub::unet::ws
