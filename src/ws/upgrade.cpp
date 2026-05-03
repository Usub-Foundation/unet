#include "unet/ws/upgrade.hpp"

#include <string>
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
            res.metadata.status_code = 400;
            res.body = "Bad Request: missing WebSocket upgrade headers";
            res.headers.addHeader("Content-Length", std::to_string(res.body.size()));
            return;
        }

        if (!validateRequest(*upgradeHeader, *connectionHeader, *versionHeader)) {
            res.metadata.status_code = 400;
            res.body = "Bad Request: invalid WebSocket upgrade";
            res.headers.addHeader("Content-Length", std::to_string(res.body.size()));
            return;
        }

        res.metadata.status_code = 101;
        res.headers.addHeader("Upgrade", "websocket");
        res.headers.addHeader("Connection", "Upgrade");
        res.headers.addHeader("Sec-WebSocket-Accept", makeAcceptKey(*key));

        ctx.accept([handler = std::move(fn)]() mutable -> usub::unet::http::SessionBox {
            return usub::unet::http::SessionBox::make<Session>(std::move(handler));
        });
    }

}  // namespace usub::unet::ws
