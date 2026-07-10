#pragma once

#include <memory>
#include <utility>

#include <uvent/Uvent.h>

#include "unet/core/io_provider.hpp"
#include "unet/core/transport/transport.hpp"
#include "unet/http/core/request.hpp"
#include "unet/http/core/response.hpp"
#include "unet/http/upgrade_context.hpp"
#include "unet/ws/core/handshake.hpp"
#include "unet/ws/handler.hpp"
#include "unet/ws/session.hpp"

namespace usub::unet::ws {

    using namespace std::string_view_literals;

    // Handles both RFC 6455 (h1) and RFC 8441 (h2) handshakes.
    template<typename HandlerT>
    usub::uvent::task::Awaitable<void>
    upgrade(usub::unet::http::RequestReader &req, usub::unet::http::ResponseWriter &res,
            usub::unet::http::UpgradeContext &ctx, std::shared_ptr<HandlerT> handler) {
        const auto versionHeader = req.headers.value("sec-websocket-version");

        if (req.metadata.version == usub::unet::http::VERSION::HTTP_2_0) {
            const auto protocol = req.headers.value(":protocol");
            if (req.metadata.method_token != "CONNECT" || !protocol || *protocol != "websocket" || !versionHeader) {
                res.metadata.status_code = 400;
                co_await res.send("Bad Request: invalid WebSocket Extended CONNECT");
                co_return;
            }
            if (*versionHeader != "13") {
                res.metadata.status_code = 400;
                co_await res.send("Bad Request: unsupported Sec-WebSocket-Version");
                co_return;
            }
            res.metadata.status_code = 200;
        } else {
            const auto upgradeHeader = req.headers.value("upgrade");
            const auto connectionHeader = req.headers.value("connection");
            const auto key = req.headers.value("sec-websocket-key");

            if (!upgradeHeader || !connectionHeader || !versionHeader || !key) {
                res.metadata.status_code = 400;
                co_await res.send("Bad Request: missing WebSocket upgrade headers");
                co_return;
            }
            if (!validateRequest(*upgradeHeader, *connectionHeader, *versionHeader)) {
                res.metadata.status_code = 400;
                co_await res.send("Bad Request: invalid WebSocket upgrade");
                co_return;
            }

            res.metadata.status_code = 101;
            res.headers.addHeader("Upgrade"sv, "websocket"sv);
            res.headers.addHeader("Connection"sv, "Upgrade"sv);
            res.headers.addHeader("Sec-WebSocket-Accept"sv, makeAcceptKey(*key));
        }

        ctx.accept([h = std::move(handler)](std::unique_ptr<usub::unet::core::transport::Transport> tr,
                                            usub::unet::Buffer carry) mutable -> usub::uvent::task::Awaitable<void> {
            auto session = std::make_shared<Session<HandlerT>>(std::move(h));
            return session->run(std::move(tr), std::move(carry));
        });
        co_return;
    }

    template<typename HandlerT>
    [[nodiscard]] auto upgradeHandler(std::shared_ptr<HandlerT> handler) {
        return [h = std::move(handler)](
                       usub::unet::http::RequestReader &req, usub::unet::http::ResponseWriter &res,
                       usub::unet::http::UpgradeContext &ctx) mutable -> usub::uvent::task::Awaitable<void> {
            co_await usub::unet::ws::upgrade<HandlerT>(req, res, ctx, h);
            co_return;
        };
    }

}// namespace usub::unet::ws
