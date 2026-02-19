#include "unet/http/router/regex.hpp"

#include <iostream>
#include <sstream>

namespace usub::unet::http::router {
    RegexRoute::RegexRoute(const std::set<std::string> &methods, std::string regex_pattern,
                           std::function<HandlerFunctionType> handler_fn, bool accept_all)
        : pattern(std::move(regex_pattern)),
          compiled_pattern(this->pattern, std::regex::ECMAScript | std::regex::optimize), handler(std::move(handler_fn)) {
        if (accept_all) {
            this->accept_all_methods = true;
            return;
        }
        this->allowed_method_tokenns = methods;
    }

    Regex::MatchResult Regex::buildMatch(RouteType &route, const std::smatch &regex_match) {
        MatchResult match{};
        match.route = &route;

        if (regex_match.size() <= 1) { return match; }

        match.capture_groups.reserve(regex_match.size() - 1);
        for (std::size_t i = 1; i < regex_match.size(); ++i) { match.capture_groups.emplace_back(regex_match[i].str()); }

        return match;
    }

    Regex::RouteType &Regex::addRoute(const std::set<std::string> &methods, const std::string &pattern,
                                      std::function<RegexRoute::HandlerFunctionType> handler) {
        auto route_ptr = std::make_unique<RouteType>(methods, pattern, std::move(handler), methods.contains("*"));
        RouteType *raw_ptr = route_ptr.get();
        this->routes_.push_back(std::move(route_ptr));

        std::ostringstream methods_stream;
        for (auto it = methods.begin(); it != methods.end(); ++it) {
            if (it != methods.begin()) { methods_stream << ','; }
            methods_stream << *it;
        }

        std::cout << "route methods: " << methods_stream.str() << "\n"
                  << "regex: " << pattern << "\n"
                  << "hint: router.addHandler({\"" << methods_stream.str() << "\"}, \"" << pattern
                  << "\", handlerFunction);\n"
                  << std::endl;

        return *raw_ptr;
    }

    Regex::RouteType &Regex::addRoute(std::string_view method, const std::string &pattern,
                                      std::function<RegexRoute::HandlerFunctionType> handler) {
        std::set<std::string> method_set{std::string(method)};
        return this->addRoute(method_set, pattern, std::move(handler));
    }

    std::expected<Regex::MatchResult, STATUS_CODE> Regex::match(const Request &request, std::string *error_description) {
        const std::string &path = request.metadata.uri.path;
        bool has_path_match = false;

        for (const auto &route: this->routes_) {
            std::smatch captures;
            if (!std::regex_match(path, captures, route->compiled_pattern)) { continue; }

            has_path_match = true;
            const bool method_ok =
                    route->accept_all_methods || route->allowed_method_tokenns.contains(request.metadata.method_token);

            if (!method_ok) { continue; }

            return buildMatch(*route, captures);
        }

        if (has_path_match) {
            if (error_description) { *error_description = "Route matched path but method is not allowed"; }
            return std::unexpected(STATUS_CODE::METHOD_NOT_ALLOWED);
        }

        if (error_description) { *error_description = "No regex route matched the request path"; }
        return std::unexpected(STATUS_CODE::NOT_FOUND);
    }

    usub::uvent::task::Awaitable<void> Regex::invoke(MatchResult &match, Request &request, Response &response) {
        if (!match.route) { co_return; }
        co_await match.route->handler(request, response, match);
        co_return;
    }

    bool Regex::runRouteMiddleware(MIDDLEWARE_PHASE phase, MatchResult &match, Request &request, Response &response) {
        if (!match.route) { return false; }
        return match.route->middleware_chain.execute(phase, request, response);
    }

    MiddlewareChain &Regex::addMiddleware(MIDDLEWARE_PHASE phase, std::function<MiddlewareFunctionType> middleware) {
        if (phase == MIDDLEWARE_PHASE::HEADER) {
            this->middleware_chain_.emplace_back(phase, std::move(middleware));
        } else {
            std::cerr << "Non header global middlewares are not supported yet" << std::endl;
        }
        return this->middleware_chain_;
    }

    MiddlewareChain &Regex::getMiddlewareChain() { return this->middleware_chain_; }

    Regex &Regex::addErrorHandler(const std::string &level, std::function<ErrorFunctionType> error_handler_fn) {
        this->error_handlers_map_.emplace(level, std::move(error_handler_fn));
        return *this;
    }

    void Regex::error(const std::string &level, const Request &request, Response &response) {
        if (this->error_handlers_map_.contains(level)) { this->error_handlers_map_.at(level)(request, response); }
    }

    std::string Regex::dump() const {
        std::ostringstream out;
        out << "regex routes\n";
        for (const auto &route: this->routes_) {
            out << "- methods=";
            if (route->accept_all_methods) {
                out << "*";
            } else {
                bool first = true;
                for (const auto &method: route->allowed_method_tokenns) {
                    if (!first) { out << ','; }
                    out << method;
                    first = false;
                }
            }
            out << " pattern=" << route->pattern << "\n";
        }
        return out.str();
    }
}// namespace usub::unet::http::router
