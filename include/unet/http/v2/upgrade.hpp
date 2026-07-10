#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <uvent/Uvent.h>

#include "unet/core/io_provider.hpp"
#include "unet/core/transport/transport.hpp"
#include "unet/http/core/request.hpp"
#include "unet/http/upgrade_context.hpp"
#include "unet/http/v2/connection.hpp"
#include "unet/http/v2/server_session.hpp"
#include "unet/http/v2/wire/frames.hpp"


namespace usub::unet::http::v2 {

    std::optional<std::vector<std::byte>> base64urlDecode(std::string_view s);

    std::optional<SettingsPayload> parseSettingsHeader(std::string_view header_value);

    inline constexpr std::string_view upgrade_response =
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Connection: Upgrade\r\n"
            "Upgrade: h2c\r\n"
            "\r\n";

    using OnH2ConnectionFn = std::function<void(Connection &)>;

    template<class RouterType>
    [[nodiscard]] auto
    makeUpgradeSession(std::shared_ptr<RouterType>         router,
                       usub::unet::http::RequestReader   &&seeded_req,
                       SettingsPayload                     seeded_settings,
                       OnH2ConnectionFn                    on_http2_connection) {
        return [router = std::move(router),
                req    = std::make_unique<usub::unet::http::RequestReader>(std::move(seeded_req)),
                settings = std::move(seeded_settings),
                on_http2_connection = std::move(on_http2_connection)](
                       std::unique_ptr<usub::unet::core::transport::Transport> tr,
                       usub::unet::Buffer                                      carry) mutable
                       -> usub::uvent::task::Awaitable<void> {
            Connection c{};
            auto session = std::make_shared<ServerSession<RouterType>>(
                    std::move(router), std::move(c), /*upgrade_path=*/true, std::move(req),
                    std::move(settings));

            if (on_http2_connection) {
                on_http2_connection(session->connection());
            }

            return session->run(std::move(tr), std::move(carry));
        };
    }

}// namespace usub::unet::http::v2
