#pragma once

#include <any>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "unet/http/middleware.hpp"
#include "unet/http/upgrade_context.hpp"

namespace usub::unet::http::router {
    struct RegexRoute;

    struct RegexMatch {
        using CaptureGroups = std::vector<std::string>;

        RegexRoute *route{nullptr};
        CaptureGroups capture_groups{};
        std::any extra{};

        std::optional<std::string_view> capture(std::size_t index) const {
            if (index >= this->capture_groups.size()) { return std::nullopt; }
            return this->capture_groups[index];
        }
    };

    struct RegexRoute {
        using HandlerFunctionType        = usub::uvent::task::Awaitable<void>(Request &, Response &, RegexMatch &);
        using UpgradeHandlerFunctionType = usub::uvent::task::Awaitable<void>(Request &, Response &,
                                                                               usub::unet::http::UpgradeContext &,
                                                                               RegexMatch &);

        usub::unet::http::RouteKind kind{usub::unet::http::RouteKind::Normal};
        bool accept_all_methods{};
        std::set<std::string> allowed_method_tokenns{};
        MiddlewareChain middleware_chain{};

        std::string pattern{};
        std::regex compiled_pattern{};
        std::function<HandlerFunctionType>        handler{};
        std::function<UpgradeHandlerFunctionType> upgrade_handler{};

        RegexRoute(const std::set<std::string> &methods, std::string regex_pattern,
                   std::function<HandlerFunctionType> handler_fn, bool accept_all = false);

        RegexRoute() = default;

        RegexRoute &addMiddleware(MIDDLEWARE_PHASE phase, std::function<MiddlewareFunctionType> middleware) {
            this->middleware_chain.emplace_back(phase, std::move(middleware));
            return *this;
        }

        template<typename Handler>
        static std::function<UpgradeHandlerFunctionType> makeUpgradeHandler(Handler &&handler_fn) {
            using HandlerType    = std::remove_reference_t<Handler>;
            using CaptureGroups  = RegexMatch::CaptureGroups;
            using Ctx            = usub::unet::http::UpgradeContext;

            if constexpr (std::is_invocable_v<HandlerType &, Request &, Response &, Ctx &, CaptureGroups &>) {
                return [fn = std::forward<Handler>(handler_fn)](Request &request, Response &response,
                                                                Ctx &ctx, RegexMatch &match) mutable
                               -> usub::uvent::task::Awaitable<void> {
                    co_await fn(request, response, ctx, match.capture_groups);
                };
            } else if constexpr (std::is_invocable_v<HandlerType &, Request &, Response &, Ctx &,
                                                      const CaptureGroups &>) {
                return [fn = std::forward<Handler>(handler_fn)](Request &request, Response &response,
                                                                Ctx &ctx, RegexMatch &match) mutable
                               -> usub::uvent::task::Awaitable<void> {
                    co_await fn(request, response, ctx, std::as_const(match.capture_groups));
                };
            } else if constexpr (std::is_invocable_v<HandlerType &, Request &, Response &, Ctx &, RegexMatch &>) {
                return [fn = std::forward<Handler>(handler_fn)](Request &request, Response &response,
                                                                Ctx &ctx, RegexMatch &match) mutable
                               -> usub::uvent::task::Awaitable<void> {
                    co_await fn(request, response, ctx, match);
                };
            } else if constexpr (std::is_invocable_v<HandlerType &, Request &, Response &, Ctx &>) {
                return [fn = std::forward<Handler>(handler_fn)](Request &request, Response &response,
                                                                Ctx &ctx, RegexMatch &) mutable
                               -> usub::uvent::task::Awaitable<void> {
                    co_await fn(request, response, ctx);
                };
            } else {
                static_assert(!sizeof(HandlerType),
                              "Upgrade handler must be invocable as "
                              "Awaitable<void>(Request&, Response&, UpgradeContext&) or "
                              "Awaitable<void>(Request&, Response&, UpgradeContext&, RegexMatch&) or "
                              "Awaitable<void>(Request&, Response&, UpgradeContext&, CaptureGroups&)");
            }
        }

        template<typename Handler>
        static std::function<HandlerFunctionType> makeHandler(Handler &&handler_fn) {
            using HandlerType = std::remove_reference_t<Handler>;
            using CaptureGroups = RegexMatch::CaptureGroups;

            if constexpr (std::is_invocable_v<HandlerType &, Request &, Response &, CaptureGroups &>) {
                return [fn = std::forward<Handler>(handler_fn)](Request &request, Response &response,
                                                                RegexMatch &match) mutable
                               -> usub::uvent::task::Awaitable<void> {
                    co_await fn(request, response, match.capture_groups);
                    co_return;
                };
            } else if constexpr (std::is_invocable_v<HandlerType &, Request &, Response &, const CaptureGroups &>) {
                return [fn = std::forward<Handler>(handler_fn)](Request &request, Response &response,
                                                                RegexMatch &match) mutable
                               -> usub::uvent::task::Awaitable<void> {
                    co_await fn(request, response, std::as_const(match.capture_groups));
                    co_return;
                };
            } else if constexpr (std::is_invocable_v<HandlerType &, Request &, Response &, RegexMatch &>) {
                return [fn = std::forward<Handler>(handler_fn)](Request &request, Response &response,
                                                                RegexMatch &match) mutable
                               -> usub::uvent::task::Awaitable<void> {
                    co_await fn(request, response, match);
                    co_return;
                };
            } else if constexpr (std::is_invocable_v<HandlerType &, Request &, Response &>) {
                return [fn = std::forward<Handler>(handler_fn)](Request &request, Response &response,
                                                                RegexMatch &) mutable
                               -> usub::uvent::task::Awaitable<void> {
                    co_await fn(request, response);
                    co_return;
                };
            } else {
                static_assert(!sizeof(HandlerType),
                              "Handler must be invocable as Awaitable<void>(Request&, Response&) or "
                              "Awaitable<void>(Request&, Response&, RegexMatch&) or "
                              "Awaitable<void>(Request&, Response&, capture_groups)");
            }
        }
    };

    using ErrorFunctionType = void(const Request &, Response &);
    using ErrorHandlers = std::unordered_map<std::string, std::function<ErrorFunctionType>>;

    class Regex {
    public:
        using RouteType = RegexRoute;
        using MatchResult = RegexMatch;

        RouteType &addRoute(const std::set<std::string> &methods, const std::string &pattern,
                            std::function<RegexRoute::HandlerFunctionType> handler);

        template<typename Handler>
        RouteType &addRoute(const std::set<std::string> &methods, const std::string &pattern, Handler &&handler) {
            return this->addRoute(methods, pattern, RouteType::makeHandler(std::forward<Handler>(handler)));
        }

        RouteType &addRoute(std::string_view method, const std::string &pattern,
                            std::function<RegexRoute::HandlerFunctionType> handler);

        template<typename Handler>
        RouteType &addRoute(std::string_view method, const std::string &pattern, Handler &&handler) {
            return this->addRoute(method, pattern, RouteType::makeHandler(std::forward<Handler>(handler)));
        }

        template<typename Handler>
        RouteType &addUpgradeRoute(std::string_view method, const std::string &pattern, Handler &&handler) {
            auto &route = this->addRoute(method, pattern,
                                         [](Request &, Response &, RegexMatch &) -> usub::uvent::task::Awaitable<void> {
                                             co_return;
                                         });
            route.kind            = usub::unet::http::RouteKind::Upgrade;
            route.upgrade_handler = RouteType::makeUpgradeHandler(std::forward<Handler>(handler));
            return route;
        }

        template<typename Handler>
        RouteType &addUpgradeRoute(const std::set<std::string> &methods, const std::string &pattern,
                                   Handler &&handler) {
            auto &route = this->addRoute(methods, pattern,
                                         [](Request &, Response &, RegexMatch &) -> usub::uvent::task::Awaitable<void> {
                                             co_return;
                                         });
            route.kind            = usub::unet::http::RouteKind::Upgrade;
            route.upgrade_handler = RouteType::makeUpgradeHandler(std::forward<Handler>(handler));
            return route;
        }

        Regex &addErrorHandler(const std::string &level, std::function<ErrorFunctionType> error_handler_fn);

        void error(const std::string &level, const Request &request, Response &response);

        std::expected<MatchResult, STATUS_CODE> match(const Request &request,
                                                      std::string *error_description = nullptr);

        usub::uvent::task::Awaitable<void> invoke(MatchResult &match, Request &request, Response &response);

        usub::uvent::task::Awaitable<void> invokeUpgrade(MatchResult &match, Request &request, Response &response,
                                                         usub::unet::http::UpgradeContext &ctx) {
            if (match.route && match.route->upgrade_handler) {
                co_await match.route->upgrade_handler(request, response, ctx, match);
            }
            co_return;
        }

        bool runRouteMiddleware(MIDDLEWARE_PHASE phase, MatchResult &match, Request &request, Response &response);

        MiddlewareChain &addMiddleware(MIDDLEWARE_PHASE phase, std::function<MiddlewareFunctionType> middleware);

        MiddlewareChain &getMiddlewareChain();

        std::string dump() const;

    private:
        ErrorHandlers error_handlers_map_;
        std::vector<std::unique_ptr<RouteType>> routes_;
        MiddlewareChain middleware_chain_;

        static MatchResult buildMatch(RouteType &route, const std::smatch &regex_match);
    };
}// namespace usub::unet::http::router
