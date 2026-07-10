#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>

#include <uvent/Uvent.h>
#include <uvent/tasks/Awaitable.h>

#include "unet/core/io_provider.hpp"
#include "unet/core/transport/transport.hpp"
#include "unet/vless/core/request.hpp"
#include "unet/vless/core/response.hpp"
#include "unet/vless/outbound.hpp"
#include "unet/vless/wire/request_parser.hpp"
#include "unet/vless/wire/response_serializer.hpp"

namespace usub::unet::vless {

    using usub::unet::core::transport::Transport;
    using usub::uvent::task::Awaitable;

    template<class AuthenticationProvider>
    class ServerSession : public std::enable_shared_from_this<ServerSession<AuthenticationProvider>> {
    public:
        explicit ServerSession(std::shared_ptr<AuthenticationProvider> auth) : auth_provider_(std::move(auth)) {}

        Awaitable<void> run(std::unique_ptr<Transport> inbound, Buffer initial_buffer) {
            return loop(this->shared_from_this(), std::move(inbound), std::move(initial_buffer));
        }

    private:
        static Awaitable<void> pump(std::shared_ptr<Transport> from, std::shared_ptr<Transport> to) {
            Buffer buf;
            for (;;) {
                buf.clear();
                const ssize_t n = co_await from->read(buf);
                if (n <= 0) break;
                const std::string_view chunk{reinterpret_cast<const char *>(buf.data()), buf.size()};
                const ssize_t sent = co_await to->send(chunk);
                if (sent < 0) break;
            }
            to->close();
            co_return;
        }

        Awaitable<void> loop(std::shared_ptr<ServerSession> self, std::unique_ptr<Transport> inbound_up,
                             Buffer buffer) {
            std::string_view buffer_view{reinterpret_cast<const char *>(buffer.data()), buffer.size()};
            std::string_view::const_iterator begin = buffer_view.begin();
            std::string_view::const_iterator end = buffer_view.end();

            auto fillIfEmpty = [&]() -> Awaitable<ssize_t> {
                if (begin != end) co_return static_cast<ssize_t>(end - begin);

                buffer.clear();
                const ssize_t n = co_await inbound_up->read(buffer);
                if (n <= 0) co_return n;

                buffer_view = std::string_view{reinterpret_cast<const char *>(buffer.data()), buffer.size()};
                begin = buffer_view.begin();
                end = buffer_view.end();
                co_return static_cast<ssize_t>(end - begin);
            };

            Request request{};
            RequestParser parser{};

            while (parser.getContext().state != RequestParser::STATE::COMPLETE &&
                   parser.getContext().state != RequestParser::STATE::FAILED) {
                if (begin == end) {
                    const ssize_t n = co_await fillIfEmpty();
                    if (n <= 0) co_return;
                }
                auto result = parser.step(request, begin, end);
                if (!result) co_return;
            }

            if (parser.getContext().state == RequestParser::STATE::FAILED) co_return;

            auto user = co_await this->auth_provider_->lookup(request);
            if (!user) co_return;

            auto outbound_up = co_await dialTcp(request.address, request.destination_port);
            if (!outbound_up) co_return;

            Response resp{};
            resp.protocol_version = 0;
            co_await inbound_up->send(ResponseSerializer::serialize(resp));

            std::shared_ptr<Transport> inbound{std::move(inbound_up)};
            std::shared_ptr<Transport> outbound{std::move(outbound_up)};

            if (begin != end) {
                const std::string_view leftover{&*begin, static_cast<std::size_t>(end - begin)};
                if (co_await outbound->send(leftover) < 0) co_return;
            }

            usub::uvent::system::co_spawn(pump(inbound, outbound));
            co_await pump(outbound, inbound);
        }

        std::shared_ptr<AuthenticationProvider> auth_provider_;
    };

}// namespace usub::unet::vless
