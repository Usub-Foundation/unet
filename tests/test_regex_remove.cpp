#include <cassert>
#include <iostream>
#include <set>
#include <string>

#include "unet/http/router/regex.hpp"

using usub::unet::http::Request;
using usub::unet::http::Response;
using usub::unet::http::STATUS_CODE;
using usub::unet::http::router::Regex;
using usub::unet::http::router::RegexMatch;

namespace {
    Request makeRequest(std::string method, std::string path) {
        Request request;
        request.metadata.method_token = std::move(method);
        request.metadata.uri.path = std::move(path);
        return request;
    }

    usub::uvent::task::Awaitable<void> noopHandler(Request &, Response &, RegexMatch &) { co_return; }
}// namespace

int main() {
    Regex router;
    router.addRoute("GET", "/users/([0-9]+)", noopHandler);

    auto request = makeRequest("GET", "/users/42");
    auto matched = router.match(request);
    assert(matched);
    assert(matched->capture(0) == "42");

    // removal is by the exact registered pattern, not by matched paths
    assert(!router.removeRoute("GET", "/users/42"));
    assert(!router.removeRoute("POST", "/users/([0-9]+)"));
    assert(router.removeRoute("GET", "/users/([0-9]+)"));

    auto request2 = makeRequest("GET", "/users/42");
    auto gone = router.match(request2);
    assert(!gone && gone.error() == STATUS_CODE::NOT_FOUND);

    router.addRoute(std::set<std::string>{"GET", "POST", "PUT"}, "/multi", noopHandler);
    assert(router.removeRoute(std::set<std::string>{"POST", "PUT", "DELETE"}, "/multi") == 2);
    auto get_request = makeRequest("GET", "/multi");
    assert(router.match(get_request));
    auto post_request = makeRequest("POST", "/multi");
    auto post_match = router.match(post_request);
    assert(!post_match && post_match.error() == STATUS_CODE::METHOD_NOT_ALLOWED);
    assert(router.removeRoute("GET", "/multi"));

    router.addRoute(std::set<std::string>{"*"}, "/any", noopHandler);
    auto any_request = makeRequest("DELETE", "/any");
    assert(router.match(any_request));
    assert(router.removeRoute("*", "/any"));
    auto any_gone_request = makeRequest("DELETE", "/any");
    assert(!router.match(any_gone_request));

    // pattern-only overload erases every route with that pattern
    router.addRoute("GET", "/dup", noopHandler);
    router.addRoute("POST", "/dup", noopHandler);
    assert(router.removeRoute("/dup"));
    auto dup_request = makeRequest("GET", "/dup");
    assert(!router.match(dup_request));
    assert(!router.removeRoute("/dup"));

    std::cout << "All regex remove tests passed.\n";
    return 0;
}
