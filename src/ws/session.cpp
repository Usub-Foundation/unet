#include "unet/ws/session.hpp"

#include <utility>

#include "unet/ws/wire/server_frame_serializer.hpp"

namespace usub::unet::ws {

    Session::Session(WsHandler handler)
        : handler_(std::move(handler)),
          channel_(std::make_shared<FrameChannel<ClientFrame>>()),
          write_state_(std::make_shared<WriteState>()) {}

    usub::uvent::task::Awaitable<usub::unet::http::SessionAction> Session::onBytes(
            std::string_view data,
            usub::unet::core::stream::Transport &transport) {
        if (!handler_spawned_) {
            write_state_->transport = &transport;
            handler_spawned_ = true;
            usub::uvent::system::co_spawn(handler_(Connection{channel_, write_state_}));
        }

        std::string_view::const_iterator begin = data.begin();
        const std::string_view::const_iterator end = data.end();
        ClientFrame frame{};

        while (begin != end) {
            auto result = parser_.step(frame, begin, end);
            if (!result) {
                co_await transport.send(
                        ServerFrameSerializer::close(CloseCode::PROTOCOL_ERROR, result.error().message));
                write_state_->transport = nullptr;
                channel_->close();
                co_return usub::unet::http::SessionAction{
                        .kind = usub::unet::http::SessionAction::Kind::Close};
            }

            if (!*result) {
                continue;
            }

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

        co_return usub::unet::http::SessionAction{
                .kind = usub::unet::http::SessionAction::Kind::Continue};
    }

    usub::uvent::task::Awaitable<void> Session::onClose() {
        write_state_->transport = nullptr;
        channel_->close();
        co_return;
    }

}  // namespace usub::unet::ws
