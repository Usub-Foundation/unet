#pragma once

#include <any>
#include <functional>
#include <memory>
#include <optional>
#include <regex>
#include <set>
#include <stack>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "unet/http/middleware.hpp"
#include "unet/utils/error.hpp"

namespace usub::unet::http::router {

    struct RadixNode;

    struct param_constraint {
        std::string pattern;
        std::string description;
    };

    struct RadixRoute;

    struct RadixMatch {
        using UriParams = std::unordered_map<std::string, std::string>;

        RadixRoute *route{nullptr};
        UriParams uri_params{};
        std::any extra{};

        std::optional<std::string_view> param(std::string_view key) const {
            const auto it = this->uri_params.find(std::string(key));
            if (it == this->uri_params.end()) { return std::nullopt; }
            return it->second;
        }
    };

    struct RadixRoute {
        using HandlerFunctionType = usub::uvent::task::Awaitable<void>(Request &, Response &, RadixMatch &);

        bool accept_all_methods{};
        std::set<std::string> allowed_method_tokenns{};
        MiddlewareChain middleware_chain{};
        std::vector<std::string> param_names{};
        std::function<HandlerFunctionType> handler{};

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
        static std::function<HandlerFunctionType> makeHandler(Handler &&handler_fn) {
            using HandlerType = std::remove_reference_t<Handler>;
            using UriParams = RadixMatch::UriParams;

            if constexpr (std::is_invocable_v<HandlerType &, Request &, Response &, UriParams &>) {
                return [fn = std::forward<Handler>(handler_fn)](Request &request, Response &response,
                                                                RadixMatch &match) mutable
                               -> usub::uvent::task::Awaitable<void> {
                    co_await fn(request, response, match.uri_params);
                    co_return;
                };
            } else if constexpr (std::is_invocable_v<HandlerType &, Request &, Response &, const UriParams &>) {
                return [fn = std::forward<Handler>(handler_fn)](Request &request, Response &response,
                                                                RadixMatch &match) mutable
                               -> usub::uvent::task::Awaitable<void> {
                    co_await fn(request, response, std::as_const(match.uri_params));
                    co_return;
                };
            } else if constexpr (std::is_invocable_v<HandlerType &, Request &, Response &, RadixMatch &>) {
                return [fn = std::forward<Handler>(handler_fn)](Request &request, Response &response,
                                                                RadixMatch &match) mutable
                               -> usub::uvent::task::Awaitable<void> {
                    co_await fn(request, response, match);
                    co_return;
                };
            } else if constexpr (std::is_invocable_v<HandlerType &, Request &, Response &>) {
                return [fn = std::forward<Handler>(handler_fn)](Request &request, Response &response,
                                                                RadixMatch &) mutable
                               -> usub::uvent::task::Awaitable<void> {
                    co_await fn(request, response);
                    co_return;
                };
            } else {
                static_assert(!sizeof(HandlerType),
                              "Handler must be invocable as Awaitable<void>(Request&, Response&) or "
                              "Awaitable<void>(Request&, Response&, RadixMatch&) or "
                              "Awaitable<void>(Request&, Response&, uri_params)");
            }
        }
    };

    struct ParamEdge {
        std::string name;
        std::optional<std::regex> regex;
        std::unique_ptr<RadixNode> child;
        std::optional<param_constraint> constraint;
    };

    const param_constraint default_constraint{R"([^/]+)", "Encountered an error..."};
    const std::unordered_map<std::string_view, const param_constraint *> no_constraints{};

    struct RadixNode {
        std::unordered_map<std::string, std::unique_ptr<RadixNode>> literal;
        std::vector<ParamEdge> param;
        std::unique_ptr<RadixNode> wildcard;
        std::string wildcard_name;
        bool trailing_slash = false;
        std::unique_ptr<RadixRoute> route = nullptr;
    };

    using ErrorFunctionType = void(const Request &, Response &);
    using ErrorHandlers = std::unordered_map<std::string, std::function<ErrorFunctionType>>;

    class Radix {
    public:
        using RouteType = RadixRoute;
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

        Radix &addErrorHandler(const std::string &level, std::function<ErrorFunctionType> error_handler_fn);

        void error(const std::string &level, const Request &request, Response &);

        std::expected<MatchResult, STATUS_CODE> match(const Request &request,
                                                      std::string *error_description = nullptr);

        usub::uvent::task::Awaitable<void> invoke(MatchResult &match, Request &request, Response &response);

        bool runRouteMiddleware(MIDDLEWARE_PHASE phase, MatchResult &match, Request &request, Response &response);

        MiddlewareChain &addMiddleware(MIDDLEWARE_PHASE phase, std::function<MiddlewareFunctionType> middleware);

        MiddlewareChain &getMiddlewareChain();

        std::string dump() const;

    private:
        ErrorHandlers error_handlers_map;
        std::unique_ptr<RadixNode> root_;
        MiddlewareChain middleware_chain_;

        struct Segment {
            enum Kind { Lit, Par, Wild } kind;
            std::string lit, name, re;
            std::optional<param_constraint> constraint;
        };

        void parsePathPattern(const std::string &pathPattern, std::regex &outRegex,
                              std::vector<std::string> &outParamNames,
                              const std::unordered_map<std::string_view, const param_constraint *> &constraints = {})
                const;

        size_t findMatchingBrace(const std::string &pathPattern, size_t start) const;

        bool containsCapturingGroup(const std::string &regex) const;

        std::vector<std::string> splitPath(const std::string &path);

        std::vector<Segment> parseSegments(const std::string &pattern, std::vector<std::string> &param_names) const;

        void applyConstraints(std::vector<Segment> &segs,
                              const std::unordered_map<std::string_view, const param_constraint *> &constraints);

        void insert(RadixNode *node, const std::vector<Segment> &segs, std::size_t idx,
                    std::unique_ptr<RouteType> &route, bool has_trailing_slash);

        bool matchDFS(RadixNode *node, const std::vector<std::string> &segs, std::size_t idx, const Request &request,
                      MatchResult &out, std::string *last_error);

        bool matchIter(RadixNode *node, const std::vector<std::string> &segs, const Request &request, MatchResult &out,
                       std::string *last_error = nullptr);

        void printNode(const RadixNode *node, std::ostringstream &buf, const std::string &prefix) const;
    };
}// namespace usub::unet::http::router
