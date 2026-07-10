#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include <uvent/Uvent.h>
#include <vector>

#include "unet/core/io_provider.hpp"
#include "unet/http/core/request.hpp"
#include "unet/http/core/response.hpp"
#include "unet/http/v1/connection.hpp"
#include "unet/http/v2/connection.hpp"
#include "unet/http/v2/frame_registry.hpp"
#include "unet/http/v2/settings_registry.hpp"
#include "unet/http/v2/wire/frames.hpp"

namespace usub::unet::core::transport {
    class Transport;
}

namespace usub::unet::http {

    enum class MIDDLEWARE_PHASE {
        HEADER,
        RESPONSE,
    };

    // Return false to halt the chain.
    using MiddlewareFunctionType = usub::uvent::task::Awaitable<bool>(RequestReader &, ResponseWriter &);

    using StatusHandlerFunctionType = usub::uvent::task::Awaitable<void>(RequestReader &, ResponseWriter &);

    class MiddlewareChain {
    private:
        std::vector<std::function<MiddlewareFunctionType>> header_middlewares_;

        std::vector<std::function<MiddlewareFunctionType>> response_middlewares_;

    public:
        MiddlewareChain &emplace_back(MIDDLEWARE_PHASE phase, std::function<MiddlewareFunctionType> middleware);
        MiddlewareChain &addMiddleware(MIDDLEWARE_PHASE phase, std::function<MiddlewareFunctionType> middleware);

        std::function<void(v1::Connection &)> on_http1_connection{};
        std::function<void(v2::Connection &, std::uint32_t /*stream_id*/)> on_http2_stream{};

        v2::Connection h2_config{};

        v2::FrameRegistry    h2_frame_handlers{};
        v2::SettingsRegistry h2_setting_handlers{};

        using H2cUpgradeFn = std::function<usub::uvent::task::Awaitable<void>(
                std::unique_ptr<usub::unet::core::transport::Transport> /*tr*/, usub::unet::Buffer /*carry*/,
                RequestReader /*seeded_request*/, v2::SettingsPayload /*seeded_settings*/)>;
        H2cUpgradeFn h2c_upgrade{};

        std::function<void(v2::Connection &)> on_http2_connection_{};

        usub::uvent::task::Awaitable<bool> execute(MIDDLEWARE_PHASE phase, RequestReader &request,
                                                   ResponseWriter &response) const;
    };


}// namespace usub::unet::http