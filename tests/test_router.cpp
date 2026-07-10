#include <cassert>
#include <cstddef>
#include <iostream>
#include <memory>
#include <string>

#include <uvent/Uvent.h>
#include <uvent/system/SystemContext.h>

#include "unet/http/core/request.hpp"
#include "unet/http/core/response.hpp"
#include "unet/http/router/radix.hpp"

using usub::unet::http::RequestReader;
using usub::unet::http::ResponseWriter;
using usub::unet::http::STATUS_CODE;
using usub::unet::http::router::Radix;
using usub::unet::http::router::RadixMatch;

namespace {

    std::size_t passed = 0;
    std::size_t failed = 0;

    void check(bool cond, const char *what) {
        if (cond) {
            ++passed;
        } else {
            ++failed;
            std::cerr << "  FAIL: " << what << "\n";
        }
    }

    RequestReader makeRequest(std::string method, std::string path) {
        RequestReader r;
        r.metadata.method_token = std::move(method);
        r.metadata.uri.path     = std::move(path);
        return r;
    }

    template<class AwaitableT>
    void runSync(usub::Uvent &uv, AwaitableT &&aw) {
        usub::uvent::system::co_spawn_static(
                [&]() -> usub::uvent::task::Awaitable<void> {
                    co_await std::forward<AwaitableT>(aw);
                    uv.stop();
                    co_return;
                }(),
                0);
        uv.run();
    }

    void registersAndMatches() {
        std::cout << "registersAndMatches\n";

        Radix router;
        bool hit_root = false;
        std::string captured_body;
        router.addRoute("GET", "/", [&](RequestReader &, ResponseWriter &res) -> usub::uvent::task::Awaitable<void> {
            hit_root = true;
            res.metadata.status_code = 200;
            // Wire a no-op send_body so the explicit send() can complete in
            // a test that doesn't have a real transport. Capture the bytes
            // the handler "sent" so the assertion below can inspect them.
            res.bindOps(usub::unet::http::ResponseWriter::Ops{
                    .send_body = [&captured_body](std::string s) -> usub::uvent::task::Awaitable<bool> {
                        captured_body = std::move(s);
                        co_return true;
                    },
            });
            co_await res.send("root");
        });

        auto req   = makeRequest("GET", "/");
        auto match = router.match(req);
        check(match.has_value(),            "GET / matched");
        check(match->route != nullptr,      "match has a route pointer");

        usub::Uvent uv{1};
        ResponseWriter res;
        runSync(uv, router.invoke(*match, req, res));
        check(hit_root,                     "handler was invoked");
        check(res.metadata.status_code == 200, "handler set status 200");
        check(captured_body == "root",      "handler sent body via send()");
        check(res.mode() == usub::unet::http::ResponseWriter::Mode::Sent,
              "send() transitions mode to Sent");
    }

    void methodMismatchReturns405() {
        std::cout << "methodMismatchReturns405\n";

        Radix router;
        router.addRoute("GET", "/only-get",
                        [](RequestReader &, ResponseWriter &) -> usub::uvent::task::Awaitable<void> { co_return; });

        auto req   = makeRequest("POST", "/only-get");
        auto match = router.match(req);
        check(!match.has_value(),                                "POST /only-get rejected");
        check(match.error() == STATUS_CODE::METHOD_NOT_ALLOWED,  "error is 405");
    }

    void missingPathReturns404() {
        std::cout << "missingPathReturns404\n";

        Radix router;
        router.addRoute("GET", "/exists",
                        [](RequestReader &, ResponseWriter &) -> usub::uvent::task::Awaitable<void> { co_return; });

        auto req   = makeRequest("GET", "/nope");
        auto match = router.match(req);
        check(!match.has_value(),                          "unknown path rejected");
        check(match.error() == STATUS_CODE::NOT_FOUND,     "error is 404");
    }

    void paramRouteCaptures() {
        std::cout << "paramRouteCaptures\n";

        Radix router;
        std::string seen_id;
        router.addRoute("GET", "/items/{id}",
                        [&](RequestReader &, ResponseWriter &res, RadixMatch &m) -> usub::uvent::task::Awaitable<void> {
                            if (auto v = m.param("id"); v) seen_id = std::string{*v};
                            res.metadata.status_code = 200;
                            co_return;
                        });

        auto req   = makeRequest("GET", "/items/42");
        auto match = router.match(req);
        check(match.has_value(),                       "/items/42 matched");
        check(match->param("id").value_or("") == "42", "match captured id=42");

        usub::Uvent uv{1};
        ResponseWriter res;
        runSync(uv, router.invoke(*match, req, res));
        check(seen_id == "42",                         "handler saw id=42");
        check(res.metadata.status_code == 200,         "status set inside handler");
    }

}// namespace

int main() {
    registersAndMatches();
    methodMismatchReturns405();
    missingPathReturns404();
    paramRouteCaptures();

    std::cout << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
