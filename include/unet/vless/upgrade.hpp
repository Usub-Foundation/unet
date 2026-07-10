#pragma once

#include <memory>
#include <utility>

#include <uvent/tasks/Awaitable.h>

#include "unet/core/io_provider.hpp"
#include "unet/core/transport/transport.hpp"
#include "unet/http/core/request.hpp"
#include "unet/http/core/response.hpp"
#include "unet/http/upgrade_context.hpp"
#include "unet/vless/server_session.hpp"

namespace usub::unet::vless {

    // Handoff: HTTP/2 stream body ⇢ VLESS session. Configure the route with a fixed path
    // (Happ side: transport = http, path = "/vless"); each POST opens a new h2 stream which
    // becomes one VLESS tunnel. h1 pure VLESS isn't wired here — VLESS over the wire needs
    // some framing above it (TLS at minimum), and unwrapping h1 to raw bytes without TLS
    // isn't useful.
    template<typename AuthenticationProvider>
    usub::uvent::task::Awaitable<void>
    upgrade(usub::unet::http::RequestReader &req, usub::unet::http::ResponseWriter &res,
            usub::unet::http::UpgradeContext &ctx, std::shared_ptr<AuthenticationProvider> auth) {
        if (req.metadata.version != usub::unet::http::VERSION::HTTP_2_0) {
            res.metadata.status_code = 400;
            co_await res.send("Bad Request: VLESS requires HTTP/2 transport");
            co_return;
        }

        res.metadata.status_code = 200;

        ctx.accept([a = std::move(auth)](std::unique_ptr<usub::unet::core::transport::Transport> tr,
                                          usub::unet::Buffer carry) mutable
                           -> usub::uvent::task::Awaitable<void> {
            auto session = std::make_shared<ServerSession<AuthenticationProvider>>(std::move(a));
            return session->run(std::move(tr), std::move(carry));
        });
        co_return;
    }

    template<typename AuthenticationProvider>
    [[nodiscard]] auto upgradeHandler(std::shared_ptr<AuthenticationProvider> auth) {
        return [a = std::move(auth)](usub::unet::http::RequestReader &req, usub::unet::http::ResponseWriter &res,
                                     usub::unet::http::UpgradeContext &ctx) mutable
                       -> usub::uvent::task::Awaitable<void> {
            co_await usub::unet::vless::upgrade<AuthenticationProvider>(req, res, ctx, a);
            co_return;
        };
    }

}// namespace usub::unet::vless
