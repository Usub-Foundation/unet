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
#include "unet/ws/wire/server_frame_serializer.hpp"

namespace usub::unet::ws {

    using WsHandler = std::function<usub::uvent::task::Awaitable<void>(Connection)>;

    class Session {
    public:
        explicit Session(WsHandler handler)
            : handler_(std::move(handler)),
              channel_(std::make_shared<FrameChannel<ClientFrame>>()),
              write_state_(std::make_shared<WriteState>()) {}

        usub::uvent::task::Awaitable<usub::unet::http::SessionAction>
        onBytes(std::string_view data, usub::unet::core::stream::Transport &transport) {
            if (!handler_spawned_) {
                write_state_->transport = &transport;
                handler_spawned_ = true;
                usub::uvent::system::co_spawn(
                    handler_(Connection{channel_, write_state_}));
            }

            std::string_view::const_iterator begin = data.begin();
            const std::string_view::const_iterator end = data.end();
            ClientFrame frame{};

            while (begin != end) {
                auto result = parser_.step(frame, begin, end);

                if (!result) {
                    co_await transport.send(
                        ServerFrameSerializer::close(CloseCode::PROTOCOL_ERROR,
                                                     result.error().message));
                    write_state_->transport = nullptr;
                    channel_->close();
                    co_return usub::unet::http::SessionAction{
                        .kind = usub::unet::http::SessionAction::Kind::Close};
                }

                if (*result) {
                    parser_.reset();

                    if (frame.opcode == static_cast<uint8_t>(Opcode::PING)) {
                        co_await transport.send(ServerFrameSerializer::pong(frame.payload));
                        frame = {};
                        continue;
                    }
                    if (frame.opcode == static_cast<uint8_t>(Opcode::CLOSE)) {
                        co_await transport.send(ServerFrameSerializer::close(CloseCode::NORMAL));
                        write_state_->transport = nullptr;
                        channel_->close();
                        co_return usub::unet::http::SessionAction{
                            .kind = usub::unet::http::SessionAction::Kind::Close};
                    }

                    channel_->push(std::move(frame));
                    frame = {};
                }
            }

            co_return usub::unet::http::SessionAction{
                .kind = usub::unet::http::SessionAction::Kind::Continue};
        }

        usub::uvent::task::Awaitable<void> onClose() {
            write_state_->transport = nullptr;
            channel_->close();
            co_return;
        }

    private:
        WsHandler                                        handler_;
        std::shared_ptr<FrameChannel<ClientFrame>>       channel_;
        std::shared_ptr<WriteState>                      write_state_;
        ClientFrameParser                                parser_{};
        bool                                             handler_spawned_{false};
    };

}  // namespace usub::unet::ws
