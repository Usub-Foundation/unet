# Writing Custom Routers

You can plug your own router into `ServerImpl` by replacing `router::Radix` with your own type.

## Where It Is Used

`ServerImpl<RouterType, ...>` and `ServerSession<VERSION::HTTP_1_1, RouterType>` call router methods directly.  
So your router must provide a compatible API surface.

## Required Router Interface

Your router type must provide:

- `using MatchResult = ...;`
- `addRoute(...)` (used by `server.handle(...)`)
- `addMiddleware(MIDDLEWARE_PHASE, std::function<MiddlewareFunctionType>)`
- `addErrorHandler(const std::string&, std::function<void(const Request&, Response&)>)`
- `error(const std::string&, const Request&, Response&)`
- `match(const Request&) -> std::expected<MatchResult, STATUS_CODE>`
- `getMiddlewareChain() -> MiddlewareChain&`
- `runRouteMiddleware(MIDDLEWARE_PHASE, MatchResult&, Request&, Response&) -> bool`
- `invoke(MatchResult&, Request&, Response&) -> Awaitable<void>`

## Minimal Skeleton

```cpp
#include <expected>
#include <functional>
#include <string>
#include <unordered_map>

#include "unet/http/core/request.hpp"
#include "unet/http/core/response.hpp"
#include "unet/http/middleware.hpp"

namespace usub::unet::http::router {
    class MyRouter {
    public:
        struct MatchResult {
            // put route-specific match context here
        };

        template<typename... Args>
        auto &addRoute(Args &&... /*unused*/) {
            // register route, return route object if you want chaining
            return *this;
        }

        MyRouter &addMiddleware(MIDDLEWARE_PHASE phase, std::function<MiddlewareFunctionType> middleware) {
            this->middleware_chain_.addMiddleware(phase, std::move(middleware));
            return *this;
        }

        MyRouter &addErrorHandler(const std::string &level, std::function<void(const Request &, Response &)> fn) {
            this->error_handlers_[level] = std::move(fn);
            return *this;
        }

        void error(const std::string &level, const Request &req, Response &res) {
            if (auto it = this->error_handlers_.find(level); it != this->error_handlers_.end()) {
                it->second(req, res);
            }
        }

        std::expected<MatchResult, STATUS_CODE> match(const Request &request) {
            (void) request;
            return MatchResult{};
        }

        MiddlewareChain &getMiddlewareChain() { return this->middleware_chain_; }

        bool runRouteMiddleware(MIDDLEWARE_PHASE phase, MatchResult &match, Request &req, Response &res) {
            (void) match;
            return this->middleware_chain_.execute(phase, req, res);
        }

        usub::uvent::task::Awaitable<void> invoke(MatchResult &match, Request &req, Response &res) {
            (void) match;
            (void) req;
            (void) res;
            co_return;
        }

    private:
        MiddlewareChain middleware_chain_{};
        std::unordered_map<std::string, std::function<void(const Request &, Response &)>> error_handlers_{};
    };
}// namespace usub::unet::http::router
```

## Use It In Server

```cpp
using MyServer = usub::unet::http::ServerImpl<
    usub::unet::http::router::MyRouter,
    usub::unet::core::stream::PlainText
>;
```

## Practical Advice

- Start by copying `router::Radix` behavior that your app needs.
- Keep `MatchResult` lightweight; it is stored in session state.
- If you support route-level middleware, ensure `runRouteMiddleware(...)` uses the route matched in `match(...)`.
