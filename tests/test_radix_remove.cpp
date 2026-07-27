#include <cassert>
#include <iostream>
#include <set>
#include <string>

#include "unet/http/router/radix.hpp"

using usub::unet::http::MIDDLEWARE_PHASE;
using usub::unet::http::Request;
using usub::unet::http::Response;
using usub::unet::http::RouteKind;
using usub::unet::http::STATUS_CODE;
using usub::unet::http::UpgradeContext;
using usub::unet::http::router::Radix;
using usub::unet::http::router::RadixMatch;

namespace {
    Request makeRequest(std::string method, std::string path) {
        Request request;
        request.metadata.method_token = std::move(method);
        request.metadata.uri.path = std::move(path);
        return request;
    }

    usub::uvent::task::Awaitable<void> noopHandler(Request &, Response &, RadixMatch &) { co_return; }

    bool dumpContains(const Radix &router, const std::string &needle) {
        return router.dump().find(needle) != std::string::npos;
    }
}// namespace

static void testTemplatePatternRemoval() {
    Radix router;
    router.addRoute("GET", "/callbacks/{bank_id}", noopHandler);

    auto request = makeRequest("GET", "/callbacks/alpha");
    auto matched = router.match(request);
    assert(matched);
    assert(matched->param("bank_id") == "alpha");

    assert(!router.removeRoute("GET", "/callbacks/alpha"));
    assert(router.removeRoute("GET", "/callbacks/{bank_id}"));

    auto request2 = makeRequest("GET", "/callbacks/alpha");
    auto rematched = router.match(request2);
    assert(!rematched && rematched.error() == STATUS_CODE::NOT_FOUND);
    assert(!dumpContains(router, "callbacks"));
}

static void testMissingPathNoOp() {
    Radix router;
    router.addRoute("GET", "/present", noopHandler);

    assert(!router.removeRoute("GET", "/absent"));
    assert(!router.removeRoute("GET", "/present/deeper"));
    assert(!router.removeRoute("POST", "/present"));
    assert(router.removeRoute(std::set<std::string>{"POST", "DELETE"}, "/present") == 0);

    auto request = makeRequest("GET", "/present");
    assert(router.match(request));
}

static void testVerbSubsetRemoval() {
    Radix router;
    router.addRoute(std::set<std::string>{"GET", "POST", "PUT"}, "/multi", noopHandler);

    assert(router.removeRoute(std::set<std::string>{"POST", "PUT", "DELETE"}, "/multi") == 2);

    auto get_request = makeRequest("GET", "/multi");
    assert(router.match(get_request));
    auto post_request = makeRequest("POST", "/multi");
    auto post_match = router.match(post_request);
    assert(!post_match && post_match.error() == STATUS_CODE::METHOD_NOT_ALLOWED);

    assert(router.removeRoute("GET", "/multi"));
    auto gone_request = makeRequest("GET", "/multi");
    auto gone = router.match(gone_request);
    assert(!gone && gone.error() == STATUS_CODE::NOT_FOUND);
    assert(!dumpContains(router, "multi"));

    router.addRoute(std::set<std::string>{"*"}, "/any", noopHandler);
    auto any_request = makeRequest("DELETE", "/any");
    assert(router.match(any_request));
    assert(router.removeRoute("*", "/any"));
    auto any_gone_request = makeRequest("DELETE", "/any");
    assert(!router.match(any_gone_request));
    assert(!dumpContains(router, "any"));
}

static void testPruning() {
    Radix router;
    router.addRoute("GET", "/deep/nested/chain/leaf", noopHandler);
    router.addRoute("GET", "/deep/other", noopHandler);
    router.addRoute("GET", "/files/*", noopHandler);

    assert(router.removeRoute("GET", "/deep/nested/chain/leaf"));
    assert(dumpContains(router, "deep"));
    assert(dumpContains(router, "other"));
    assert(!dumpContains(router, "nested"));
    assert(!dumpContains(router, "chain"));
    assert(!dumpContains(router, "leaf"));

    auto other_request = makeRequest("GET", "/deep/other");
    assert(router.match(other_request));

    auto tail_request = makeRequest("GET", "/files/js/app.js");
    assert(router.match(tail_request));
    assert(router.removeRoute("/files/*"));
    auto tail_gone_request = makeRequest("GET", "/files/js/app.js");
    assert(!router.match(tail_gone_request));
    assert(!dumpContains(router, "files"));
}

static void testUpgradeRouteRemoval() {
    Radix router;
    router.addUpgradeRoute("GET", "/ws",
                           [](Request &, Response &, UpgradeContext &) -> usub::uvent::task::Awaitable<void> {
                               co_return;
                           });

    auto request = makeRequest("GET", "/ws");
    auto matched = router.match(request);
    assert(matched);
    assert(matched->route->kind == RouteKind::Upgrade);
    assert(matched->route->upgrade_handler != nullptr);

    assert(router.removeRoute("GET", "/ws"));
    auto gone_request = makeRequest("GET", "/ws");
    auto gone = router.match(gone_request);
    assert(!gone && gone.error() == STATUS_CODE::NOT_FOUND);
    assert(!dumpContains(router, "ws"));
}

static void testMiddlewareLifetime() {
    Radix router;
    Response response;

    router.addRoute("GET", "/mw", noopHandler)
            .addMiddleware(MIDDLEWARE_PHASE::HEADER, [](Request &, Response &) { return false; });

    auto request = makeRequest("GET", "/mw");
    auto matched = router.match(request);
    assert(matched);
    assert(!router.runRouteMiddleware(MIDDLEWARE_PHASE::HEADER, *matched, request, response));

    assert(router.removeRoute("GET", "/mw"));
    router.addRoute("GET", "/mw", noopHandler);
    auto request2 = makeRequest("GET", "/mw");
    auto rematched = router.match(request2);
    assert(rematched);
    assert(router.runRouteMiddleware(MIDDLEWARE_PHASE::HEADER, *rematched, request2, response));

    // re-registering (method, pattern) replaces handler and middleware in place
    router.addRoute("GET", "/replace", noopHandler)
            .addMiddleware(MIDDLEWARE_PHASE::HEADER, [](Request &, Response &) { return false; });
    router.addRoute("GET", "/replace", noopHandler);
    auto request3 = makeRequest("GET", "/replace");
    auto replaced = router.match(request3);
    assert(replaced);
    assert(router.runRouteMiddleware(MIDDLEWARE_PHASE::HEADER, *replaced, request3, response));
}

int main() {
    testTemplatePatternRemoval();
    testMissingPathNoOp();
    testVerbSubsetRemoval();
    testPruning();
    testUpgradeRouteRemoval();
    testMiddlewareLifetime();

    std::cout << "All radix remove tests passed.\n";
    return 0;
}
