#include "unet/ws/upgrade.hpp"

#include <utility>

namespace usub::unet::ws {

    void upgrade(usub::unet::http::Request &req,
                 usub::unet::http::Response &res,
                 usub::unet::http::UpgradeContext &ctx,
                 WsHandler fn) {
        const auto upgradeHeader = req.headers.value("upgrade");
        const auto connectionHeader = req.headers.value("connection");
        const auto versionHeader = req.headers.value("sec-websocket-version");
        const auto key = req.headers.value("sec-websocket-key");

        if (!upgradeHeader || !connectionHeader || !versionHeader || !key) {
            res.setStatus(400);
            res.setBody("Bad Request: missing WebSocket upgrade headers");
            return;
        }

        if (!validateRequest(*upgradeHeader, *connectionHeader, *versionHeader)) {
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

}  // namespace usub::unet::ws
