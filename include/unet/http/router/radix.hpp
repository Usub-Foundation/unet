#pragma once

#include <any>
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

#include "unet/http/core/request.hpp"
#include "unet/http/core/response.hpp"
#include "unet/http/middleware.hpp"
#include "unet/http/session.hpp"
#include "unet/http/upgrade_context.hpp"
#include "unet/utils/error.hpp"

namespace usub::unet::http::router {

    struct RadixNode;

    struct param_constraint {
        std::string pattern;
        std::string description;
    };

    struct RadixRoute;

    // Captured params, in registration order. Backed by string_view slices over
    // the request's path — zero-allocation in the hot path. Keys are the param
    // names (`{id}` → `"id"`, `*splat` → `"splat"`).
    struct RadixMatch {
        using UriParams = std::unordered_map<std::string_view, std::string_view>;

        RadixRoute *route{nullptr};
        UriParams   uri_params{};
        std::any    extra{};

        std::optional<std::string_view> param(std::string_view key) const {
            const auto it = this->uri_params.find(key);
            if (it == this->uri_params.end()) return std::nullopt;
            return it->second;
        }
    };

    struct RadixRoute {
        using HandlerFunctionType        = usub::uvent::task::Awaitable<void>(RequestReader &, ResponseWriter &,
                                                                               RadixMatch &);
        using UpgradeHandlerFunctionType = usub::uvent::task::Awaitable<void>(RequestReader &, ResponseWriter &,
                                                                               usub::unet::http::UpgradeContext &,
                                                                               RadixMatch &);

        usub::unet::http::RouteKind         kind{usub::unet::http::RouteKind::Normal};
        bool                                accept_all_methods{};
        std::set<std::string>               allowed_method_tokenns{};
        MiddlewareChain                     middleware_chain{};
        std::vector<std::string>            param_names{};
        std::function<HandlerFunctionType>        handler{};
        std::function<UpgradeHandlerFunctionType> upgrade_handler{};

        RadixRoute(const std::set<std::string> &methods, const std::vector<std::string> &params,
                   std::function<HandlerFunctionType> handler_fn, bool accept_all = false)
            : param_names(params), handler(std::move(handler_fn)) {
            if (accept_all) {
                this->accept_all_methods = true;
                return;
            }
            this->allowed_method_tokenns = methods;
        }

        RadixRoute() = default;

        RadixRoute &addMiddleware(MIDDLEWARE_PHASE phase, std::function<MiddlewareFunctionType> middleware) {
            this->middleware_chain.emplace_back(phase, std::move(middleware));
            return *this;
        }

        template<typename Handler>
        static std::function<UpgradeHandlerFunctionType> makeUpgradeHandler(Handler &&handler_fn) {
            using HandlerType = std::remove_reference_t<Handler>;
            using UriParams   = RadixMatch::UriParams;
            using Ctx         = usub::unet::http::UpgradeContext;

            if constexpr (std::is_invocable_v<HandlerType &, RequestReader &, ResponseWriter &, Ctx &, UriParams &>) {
                return [fn = std::forward<Handler>(handler_fn)](RequestReader &request, ResponseWriter &response,
                                                                Ctx &ctx, RadixMatch &match) mutable
                               -> usub::uvent::task::Awaitable<void> {
                    co_await fn(request, response, ctx, match.uri_params);
                };
            } else if constexpr (std::is_invocable_v<HandlerType &, RequestReader &, ResponseWriter &, Ctx &,
                                                       const UriParams &>) {
                return [fn = std::forward<Handler>(handler_fn)](RequestReader &request, ResponseWriter &response,
                                                                Ctx &ctx, RadixMatch &match) mutable
                               -> usub::uvent::task::Awaitable<void> {
                    co_await fn(request, response, ctx, std::as_const(match.uri_params));
                };
            } else if constexpr (std::is_invocable_v<HandlerType &, RequestReader &, ResponseWriter &, Ctx &,
                                                       RadixMatch &>) {
                return [fn = std::forward<Handler>(handler_fn)](RequestReader &request, ResponseWriter &response,
                                                                Ctx &ctx, RadixMatch &match) mutable
                               -> usub::uvent::task::Awaitable<void> {
                    co_await fn(request, response, ctx, match);
                };
            } else if constexpr (std::is_invocable_v<HandlerType &, RequestReader &, ResponseWriter &, Ctx &>) {
                return [fn = std::forward<Handler>(handler_fn)](RequestReader &request, ResponseWriter &response,
                                                                Ctx &ctx, RadixMatch &) mutable
                               -> usub::uvent::task::Awaitable<void> {
                    co_await fn(request, response, ctx);
                };
            } else {
                static_assert(!sizeof(HandlerType),
                              "Upgrade handler must be invocable as "
                              "Awaitable<void>(RequestReader&, ResponseWriter&, UpgradeContext&) or "
                              "Awaitable<void>(RequestReader&, ResponseWriter&, UpgradeContext&, RadixMatch&) or "
                              "Awaitable<void>(RequestReader&, ResponseWriter&, UpgradeContext&, UriParams&)");
            }
        }

        template<typename Handler>
        static std::function<HandlerFunctionType> makeHandler(Handler &&handler_fn) {
            using HandlerType = std::remove_reference_t<Handler>;
            using UriParams = RadixMatch::UriParams;

            if constexpr (std::is_invocable_v<HandlerType &, RequestReader &, ResponseWriter &, UriParams &>) {
                return [fn = std::forward<Handler>(handler_fn)](RequestReader &request, ResponseWriter &response,
                                                                RadixMatch &match) mutable
                               -> usub::uvent::task::Awaitable<void> {
                    co_await fn(request, response, match.uri_params);
                };
            } else if constexpr (std::is_invocable_v<HandlerType &, RequestReader &, ResponseWriter &,
                                                       const UriParams &>) {
                return [fn = std::forward<Handler>(handler_fn)](RequestReader &request, ResponseWriter &response,
                                                                RadixMatch &match) mutable
                               -> usub::uvent::task::Awaitable<void> {
                    co_await fn(request, response, std::as_const(match.uri_params));
                };
            } else if constexpr (std::is_invocable_v<HandlerType &, RequestReader &, ResponseWriter &, RadixMatch &>) {
                return [fn = std::forward<Handler>(handler_fn)](RequestReader &request, ResponseWriter &response,
                                                                RadixMatch &match) mutable
                               -> usub::uvent::task::Awaitable<void> {
                    co_await fn(request, response, match);
                };
            } else if constexpr (std::is_invocable_v<HandlerType &, RequestReader &, ResponseWriter &>) {
                return [fn = std::forward<Handler>(handler_fn)](RequestReader &request, ResponseWriter &response,
                                                                RadixMatch &) mutable
                               -> usub::uvent::task::Awaitable<void> {
                    co_await fn(request, response);
                };
            } else {
                static_assert(!sizeof(HandlerType),
                              "Handler must be invocable as "
                              "Awaitable<void>(RequestReader&, ResponseWriter&) or "
                              "Awaitable<void>(RequestReader&, ResponseWriter&, RadixMatch&) or "
                              "Awaitable<void>(RequestReader&, ResponseWriter&, uri_params)");
            }
        }
    };

    struct ParamEdge {
        std::string                     name;
        std::optional<std::regex>       regex;
        std::optional<param_constraint> constraint;
        std::unique_ptr<RadixNode>      child;
    };

    struct WildcardEdge {
        std::string                name;
        std::unique_ptr<RadixNode> child;
    };

    const param_constraint default_constraint{R"([^/]+)", "Encountered an error..."};
    const std::unordered_map<std::string_view, const param_constraint *> no_constraints{};

    struct RadixNode {
        std::vector<std::pair<std::string, std::unique_ptr<RadixNode>>> literal_edges;

        std::unique_ptr<ParamEdge>    param_edge;
        std::unique_ptr<WildcardEdge> wildcard_edge;

        std::unique_ptr<RadixRoute> route;
        bool                        trailing_slash{false};
    };

    using ErrorFunctionType = usub::uvent::task::Awaitable<void>(RequestReader &, ResponseWriter &);
    using ErrorHandlers     = std::unordered_map<std::string, std::function<ErrorFunctionType>>;

    class Radix {
    public:
        using RouteType   = RadixRoute;
        using MatchResult = RadixMatch;

        Radix() : root_(std::make_unique<RadixNode>()) {}

        RouteType &addRoute(const std::set<std::string> &methods, const std::string &pattern,
                            std::function<RadixRoute::HandlerFunctionType> handler,
                            const std::unordered_map<std::string_view, const param_constraint *> &constraints =
                                    no_constraints);

        template<typename Handler>
        RouteType &addRoute(const std::set<std::string> &methods, const std::string &pattern, Handler &&handler,
                            const std::unordered_map<std::string_view, const param_constraint *> &constraints =
                                    no_constraints) {
            return this->addRoute(methods, pattern, RouteType::makeHandler(std::forward<Handler>(handler)),
                                  constraints);
        }

        RouteType &addRoute(std::string_view method, const std::string &pathPattern,
                            std::function<RadixRoute::HandlerFunctionType> function,
                            const std::unordered_map<std::string_view, const param_constraint *> &constraints =
                                    no_constraints);

        template<typename Handler>
        RouteType &addRoute(std::string_view method, const std::string &pathPattern, Handler &&function,
                            const std::unordered_map<std::string_view, const param_constraint *> &constraints =
                                    no_constraints) {
            return this->addRoute(method, pathPattern, RouteType::makeHandler(std::forward<Handler>(function)),
                                  constraints);
        }

        template<typename Handler>
        RouteType &addUpgradeRoute(std::string_view method, const std::string &pattern, Handler &&handler,
                                   const std::unordered_map<std::string_view, const param_constraint *> &constraints =
                                           no_constraints) {
            auto &route = this->addRoute(method, pattern,
                                         [](RequestReader &, ResponseWriter &, RadixMatch &)
                                                 -> usub::uvent::task::Awaitable<void> { co_return; },
                                         constraints);
            route.kind            = usub::unet::http::RouteKind::Upgrade;
            route.upgrade_handler = RouteType::makeUpgradeHandler(std::forward<Handler>(handler));
            return route;
        }

        template<typename Handler>
        RouteType &addUpgradeRoute(const std::set<std::string> &methods, const std::string &pattern,
                                   Handler &&handler,
                                   const std::unordered_map<std::string_view, const param_constraint *> &constraints =
                                           no_constraints) {
            auto &route = this->addRoute(methods, pattern,
                                         [](RequestReader &, ResponseWriter &, RadixMatch &)
                                                 -> usub::uvent::task::Awaitable<void> { co_return; },
                                         constraints);
            route.kind            = usub::unet::http::RouteKind::Upgrade;
            route.upgrade_handler = RouteType::makeUpgradeHandler(std::forward<Handler>(handler));
            return route;
        }

        Radix &addErrorHandler(const std::string &level, std::function<ErrorFunctionType> error_handler_fn);

        usub::uvent::task::Awaitable<void> error(const std::string &level, RequestReader &request, ResponseWriter &);

        std::expected<MatchResult, STATUS_CODE> match(const RequestReader &request,
                                                      std::string *error_description = nullptr);

        usub::uvent::task::Awaitable<void> invoke(MatchResult &match, RequestReader &request, ResponseWriter &response);

        usub::uvent::task::Awaitable<void> invokeUpgrade(MatchResult &match, RequestReader &request,
                                                         ResponseWriter &response,
                                                         usub::unet::http::UpgradeContext &ctx) {
            if (match.route && match.route->upgrade_handler) {
                co_await match.route->upgrade_handler(request, response, ctx, match);
            }
            co_return;
        }

        usub::uvent::task::Awaitable<bool>
        runRouteMiddleware(MIDDLEWARE_PHASE phase, MatchResult &match, RequestReader &request,
                           ResponseWriter &response);

        MiddlewareChain &addMiddleware(MIDDLEWARE_PHASE phase, std::function<MiddlewareFunctionType> middleware);

        MiddlewareChain &getMiddlewareChain();

        std::string dump() const;

    private:
        ErrorHandlers              error_handlers_map;
        std::unique_ptr<RadixNode> root_;
        MiddlewareChain            middleware_chain_;

        // ---- pattern tokenization -----------------------------------------

        // A pattern is a sequence of these tokens. Literal runs absorb any
        // chars not interpreted as the start of `{...}` or `*name`.
        struct PatternToken {
            enum class Kind { Literal, Param, Wildcard } kind{Kind::Literal};
            std::string                     text;   // literal text OR param/wildcard name
            std::string                     regex;  // only for Param
            std::optional<param_constraint> constraint;
        };

        std::vector<PatternToken>
        tokenize(const std::string &pattern, std::vector<std::string> &param_names,
                 const std::unordered_map<std::string_view, const param_constraint *> &constraints) const;

        void insert(RadixNode *node, const std::vector<PatternToken> &tokens, std::size_t idx,
                    std::string_view literal_remaining,
                    std::unique_ptr<RouteType> &route, bool has_trailing_slash);

        bool matchAt(RadixNode *node, std::string_view path, std::string_view cursor, const RequestReader &request,
                     MatchResult &out, std::string *last_error) const;

        void printNode(const RadixNode *node, std::ostringstream &buf, const std::string &prefix) const;
    };

}// namespace usub::unet::http::router
