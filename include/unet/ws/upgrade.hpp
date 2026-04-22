#pragma once

// ws::upgrade - wires a WebSocket handler into an HTTP upgrade route.

#include <utility>

#include <uvent/Uvent.h>

#include "unet/http/core/request.hpp"
#include "unet/http/core/response.hpp"
#include "unet/http/upgrade_context.hpp"
#include "unet/ws/core/handshake.hpp"
#include "unet/ws/session.hpp"

namespace usub::unet::ws {

    void upgrade(usub::unet::http::Request &req,
                 usub::unet::http::Response &res,
                 usub::unet::http::UpgradeContext &ctx,
                 WsHandler fn);

    template<typename Handler>
    [[nodiscard]] auto upgradeHandler(Handler &&wsHandler) {
        return [fn = WsHandler(std::forward<Handler>(wsHandler))](
                       usub::unet::http::Request &req,
                       usub::unet::http::Response &res,
                       usub::unet::http::UpgradeContext &ctx) mutable
                   -> usub::uvent::task::Awaitable<void> {
            usub::unet::ws::upgrade(req, res, ctx, fn);
            co_return;
        };
    }

}  // namespace usub::unet::ws
