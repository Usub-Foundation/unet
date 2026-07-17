# Custom routers

`ServerImpl` is templated on the router type. You can swap `router::Radix` for your own & keep the rest of the framework.

## Where the router is used

`ServerImpl<Router, ...>`, `v1::ServerSession<Router>`, & `v2::ServerSession<Router>` all call into the router directly. Your type has to expose a compatible surface, listed below.

## Required interface

Your router must provide:

- `using MatchResult = ...;`
- `addRoute(...)` - what `server.handle(...)` forwards to.
- `addUpgradeRoute(...)` - what `server.handleUpgrade(...)` forwards to.
- `addMiddleware(MIDDLEWARE_PHASE, std::function<MiddlewareFunctionType>)`
- `addErrorHandler(std::string, error_handler_fn)`
- `error(std::string level, RequestReader &, ResponseWriter &) -> Awaitable<void>`
- `match(const RequestReader &) -> std::expected<MatchResult, STATUS_CODE>`
- `getMiddlewareChain() -> MiddlewareChain &`
- `runRouteMiddleware(MIDDLEWARE_PHASE, MatchResult &, RequestReader &, ResponseWriter &) -> Awaitable<bool>`
- `invoke(MatchResult &, RequestReader &, ResponseWriter &) -> Awaitable<void>`
- `invokeUpgrade(MatchResult &, RequestReader &, ResponseWriter &, UpgradeContext &) -> Awaitable<void>`

`MiddlewareFunctionType` is `Awaitable<bool>(RequestReader &, ResponseWriter &)`.

## Minimal skeleton

```cpp
#include <expected>
#include <functional>
#include <string>
#include <unordered_map>

#include "unet/http/core/request.hpp"
#include "unet/http/core/response.hpp"
#include "unet/http/middleware.hpp"
#include "unet/http/upgrade_context.hpp"

namespace usub::unet::http::router {

    class MyRouter {
    public:
        struct MatchResult {
            // Per-request match context. Kept in session state.
        };

        template<typename... Args>
        auto &addRoute(Args &&...) {
            // Register route. Return route object if you want per-route chaining.
            return *this;
        }

        template<typename... Args>
        auto &addUpgradeRoute(Args &&...) {
            return *this;
        }

        MyRouter &addMiddleware(MIDDLEWARE_PHASE phase,
                                std::function<MiddlewareFunctionType> mw) {
            this->chain_.addMiddleware(phase, std::move(mw));
            return *this;
        }

        template<typename Fn>
        MyRouter &addErrorHandler(std::string key, Fn fn) {
            this->error_handlers_[std::move(key)] = std::move(fn);
            return *this;
        }

        usub::uvent::task::Awaitable<void>
        error(std::string key,
              usub::unet::http::RequestReader &req,
              usub::unet::http::ResponseWriter &res) {
            auto it = this->error_handlers_.find(key);
            if (it != this->error_handlers_.end()) co_await it->second(req, res);
            co_return;
        }

        std::expected<MatchResult, STATUS_CODE>
        match(const usub::unet::http::RequestReader &) {
            return MatchResult{};
        }

        MiddlewareChain &getMiddlewareChain() { return this->chain_; }

        usub::uvent::task::Awaitable<bool>
        runRouteMiddleware(MIDDLEWARE_PHASE phase, MatchResult &,
                           usub::unet::http::RequestReader &req,
                           usub::unet::http::ResponseWriter &res) {
            co_return co_await this->chain_.execute(phase, req, res);
        }

        usub::uvent::task::Awaitable<void>
        invoke(MatchResult &,
               usub::unet::http::RequestReader &,
               usub::unet::http::ResponseWriter &) {
            co_return;
        }

        usub::uvent::task::Awaitable<void>
        invokeUpgrade(MatchResult &,
                      usub::unet::http::RequestReader &,
                      usub::unet::http::ResponseWriter &,
                      usub::unet::http::UpgradeContext &) {
            co_return;
        }

    private:
        MiddlewareChain chain_{};
        std::unordered_map<std::string,
            std::function<usub::uvent::task::Awaitable<void>(RequestReader &, ResponseWriter &)>>
                error_handlers_{};
    };

}// namespace usub::unet::http::router
```

## Wire it into a server

```cpp
using MyServer = usub::unet::http::ServerImpl<
    usub::unet::http::router::MyRouter,
    usub::unet::core::stream::PlainText
>;

MyServer server{runtime, config};
```

## Practical notes

- Start by looking at `router::Radix` in `include/unet/http/router/radix.hpp` & copying the parts you need.
- Keep `MatchResult` cheap - it's copied around per request.
- If you support route-level middleware, `runRouteMiddleware` must dispatch through the matched route's chain, not the global chain.
- `RouteKind::Upgrade` on the route lets sessions branch to `invokeUpgrade` instead of `invoke`. Your router should tag upgrade routes accordingly.
