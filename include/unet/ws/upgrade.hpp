#pragma once

// ws::upgrade — wires a WebSocket handler into an HTTP upgrade route.
//
// Simple use:
//   server.handleUpgrade("GET", "/ws",
//       ws::upgradeHandler([](ws::Connection conn) -> Awaitable<void> {
//           while (auto frame = co_await conn.recv()) { ... }
//       }));
//
// Advanced use (access request headers, URI params, modify handshake response):
//   server.handleUpgrade("GET", "/chat/:room",
//       [](Request& req, Response& res, UpgradeContext& ctx, std::string_view room) -> Awaitable<void> {
//           ws::upgrade(req, res, ctx, make_handler(room));
//           res.addHeader("X-Room", room);  // still free to modify after ws::upgrade
//       });

#include <uvent/Uvent.h>

#include "unet/http/core/request.hpp"
#include "unet/http/core/response.hpp"
#include "unet/http/session.hpp"
#include "unet/http/upgrade_context.hpp"
#include "unet/ws/core/handshake.hpp"
#include "unet/ws/session.hpp"

namespace usub::unet::ws {

    // Free function: validates WS headers, sets 101 handshake on res, signals ctx.accept().
    // If headers are invalid, sets res to 400 and returns without calling ctx.accept().
    // The handler may add more response headers after calling ws::upgrade().
    inline void upgrade(usub::unet::http::Request &req, usub::unet::http::Response &res,
                        usub::unet::http::UpgradeContext &ctx, WsHandler fn) {
        const auto upgrade_hdr = req.headers.value("upgrade");
        const auto connection  = req.headers.value("connection");
        const auto version     = req.headers.value("sec-websocket-version");
        const auto key         = req.headers.value("sec-websocket-key");

        if (!upgrade_hdr || !connection || !version || !key) {
            res.setStatus(400);
            res.setBody("Bad Request: missing WebSocket upgrade headers");
            return;
        }

        if (!validateRequest(*upgrade_hdr, *connection, *version)) {
            res.setStatus(400);
            res.setBody("Bad Request: invalid WebSocket upgrade");
            return;
        }

        res.setStatus(101);
        res.addHeader("Upgrade", "websocket");
        res.addHeader("Connection", "Upgrade");
        res.addHeader("Sec-WebSocket-Accept", makeAcceptKey(*key));

        ctx.accept([handler = std::move(fn)]() mutable -> usub::unet::http::SessionBox {
            return usub::unet::http::SessionBox::make<Session>(std::move(handler));
        });
    }

    // Convenience wrapper: produces a complete handleUpgrade-compatible handler from just a WsHandler.
    template<typename Handler>
    [[nodiscard]] auto upgradeHandler(Handler &&ws_handler) {
        return [fn = WsHandler(std::forward<Handler>(ws_handler))](
                   usub::unet::http::Request &req, usub::unet::http::Response &res,
                   usub::unet::http::UpgradeContext &ctx) mutable
               -> usub::uvent::task::Awaitable<void> {
            usub::unet::ws::upgrade(req, res, ctx, fn);
            co_return;
        };
    }

}  // namespace usub::unet::ws
