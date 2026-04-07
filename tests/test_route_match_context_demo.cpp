#include <charconv>
#include <functional>
#include <iostream>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

// Single-file prototype demonstrating how to keep URI params out of Request.
// Build locally (example):
// g++ -std=c++23 -O2 -Wall -Wextra -pedantic tests/test_route_match_context_demo.cpp -o /tmp/route_demo

struct Request {
    std::string method;
    std::string path;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    // No uri_params here.
};

struct Response {
    int status = 200;
    std::string body;
};

struct RouteParams {
    std::vector<std::pair<std::string, std::string>> values;

    std::optional<std::string_view> get(std::string_view key) const {
        for (const auto &[k, v] : values) {
            if (k == key) {
                return v;
            }
        }
        return std::nullopt;
    }

    template <typename T>
    std::optional<T> as(std::string_view key) const {
        const auto v = get(key);
        if (!v) {
            return std::nullopt;
        }

        if constexpr (std::is_same_v<T, std::string>) {
            return std::string(*v);
        } else if constexpr (std::is_same_v<T, std::string_view>) {
            return *v;
        } else if constexpr (std::is_integral_v<T>) {
            T out{};
            const auto *begin = v->data();
            const auto *end = begin + v->size();
            const auto [ptr, ec] = std::from_chars(begin, end, out);
            if (ec == std::errc() && ptr == end) {
                return out;
            }
            return std::nullopt;
        } else {
            static_assert(!sizeof(T), "RouteParams::as<T> supports std::string, std::string_view, integral types");
        }
    }

    void upsert(std::string key, std::string value) {
        for (auto &[k, v] : values) {
            if (k == key) {
                v = std::move(value);
                return;
            }
        }
        values.emplace_back(std::move(key), std::move(value));
    }
};

struct Route;

struct RouteMatch {
    const Route *route = nullptr;
    RouteParams params;
};

class RouteInvoker {
public:
    using NewSignature = std::function<void(Request &, Response &, const RouteMatch &)>;

    RouteInvoker() = default;

    template <typename F>
    static RouteInvoker make(F &&f) {
        using Fn = std::remove_reference_t<F>;

        if constexpr (std::is_invocable_r_v<void, Fn &, Request &, Response &, const RouteMatch &>) {
            return RouteInvoker(NewSignature(std::forward<F>(f)));
        } else if constexpr (std::is_invocable_r_v<void, Fn &, Request &, Response &>) {
            return RouteInvoker(NewSignature(
                [legacy = std::forward<F>(f)](Request &req, Response &res, const RouteMatch &) mutable {
                    legacy(req, res);
                }
            ));
        } else {
            static_assert(!sizeof(Fn), "Handler must be invocable as void(Request&, Response&) or void(Request&, Response&, const RouteMatch&)");
        }
    }

    void operator()(Request &req, Response &res, const RouteMatch &match) const {
        fn_(req, res, match);
    }

private:
    explicit RouteInvoker(NewSignature fn) : fn_(std::move(fn)) {}

    NewSignature fn_ = [](Request &, Response &, const RouteMatch &) {};
};

struct Route {
    std::string method;
    std::string pattern;
    std::vector<std::string> segments;
    RouteInvoker handler;
};

class Router {
public:
    template <typename F>
    void add(std::string method, std::string pattern, F &&handler) {
        Route route;
        route.method = std::move(method);
        route.pattern = std::move(pattern);
        route.segments = split(route.pattern);
        route.handler = RouteInvoker::make(std::forward<F>(handler));
        routes_.push_back(std::move(route));
    }

    std::optional<RouteMatch> match(const Request &request) const {
        const auto req_segments = split(request.path);

        for (const Route &route : routes_) {
            if (route.method != "*" && route.method != request.method) {
                continue;
            }

            const auto params = match_segments(route.segments, req_segments);
            if (!params) {
                continue;
            }

            return RouteMatch{
                .route = &route,
                .params = std::move(*params),
            };
        }
        return std::nullopt;
    }

    bool dispatch(Request &request, Response &response) const {
        const auto matched = match(request);
        if (!matched) {
            response.status = 404;
            response.body = "not found";
            return false;
        }

        matched->route->handler(request, response, *matched);
        return true;
    }

private:
    static std::vector<std::string> split(std::string_view path) {
        std::vector<std::string> out;
        std::string current;

        for (char ch : path) {
            if (ch == '/') {
                if (!current.empty()) {
                    out.push_back(current);
                    current.clear();
                }
                continue;
            }
            current.push_back(ch);
        }

        if (!current.empty()) {
            out.push_back(std::move(current));
        }

        return out;
    }

    static std::optional<RouteParams> match_segments(
        const std::vector<std::string> &route_segments,
        const std::vector<std::string> &request_segments
    ) {
        RouteParams params;

        std::size_t i = 0;
        std::size_t j = 0;
        while (i < route_segments.size() && j < request_segments.size()) {
            const std::string &rseg = route_segments[i];
            const std::string &qseg = request_segments[j];

            if (!rseg.empty() && rseg[0] == ':') {
                params.upsert(rseg.substr(1), qseg);
                ++i;
                ++j;
                continue;
            }

            if (rseg == "*") {
                std::string tail;
                for (std::size_t k = j; k < request_segments.size(); ++k) {
                    if (!tail.empty()) {
                        tail.push_back('/');
                    }
                    tail += request_segments[k];
                }
                params.upsert("*", std::move(tail));
                return params;
            }

            if (rseg != qseg) {
                return std::nullopt;
            }

            ++i;
            ++j;
        }

        if (i == route_segments.size() && j == request_segments.size()) {
            return params;
        }
        return std::nullopt;
    }

    std::vector<Route> routes_;
};

int main() {
    Router router;

    // Old-style handler still works via adapter.
    router.add("GET", "/health", [](Request &, Response &res) {
        res.body = "ok";
    });

    // New-style handler receives route params via RouteMatch.
    router.add("GET", "/users/:id/books/:book_id", [](Request &, Response &res, const RouteMatch &match) {
        const auto user_id = match.params.as<int>("id");
        const auto book_id = match.params.as<int>("book_id");

        if (!user_id || !book_id) {
            res.status = 400;
            res.body = "invalid params";
            return;
        }

        res.body = "user=" + std::to_string(*user_id) + " book=" + std::to_string(*book_id);
    });

    // Wildcard example
    router.add("GET", "/static/*", [](Request &, Response &res, const RouteMatch &match) {
        const auto tail = match.params.get("*").value_or("<none>");
        res.body = "static tail=" + std::string(tail);
    });

    {
        Request req{.method = "GET", .path = "/health", .headers = {}, .body = ""};
        Response res;
        router.dispatch(req, res);
        std::cout << "GET /health -> " << res.status << " | " << res.body << '\n';
    }

    {
        Request req{.method = "GET", .path = "/users/42/books/7", .headers = {}, .body = ""};
        Response res;
        router.dispatch(req, res);
        std::cout << "GET /users/42/books/7 -> " << res.status << " | " << res.body << '\n';
    }

    {
        Request req{.method = "GET", .path = "/static/js/app.js", .headers = {}, .body = ""};
        Response res;
        router.dispatch(req, res);
        std::cout << "GET /static/js/app.js -> " << res.status << " | " << res.body << '\n';
    }

    {
        Request req{.method = "GET", .path = "/missing", .headers = {}, .body = ""};
        Response res;
        router.dispatch(req, res);
        std::cout << "GET /missing -> " << res.status << " | " << res.body << '\n';
    }

    return 0;
}
