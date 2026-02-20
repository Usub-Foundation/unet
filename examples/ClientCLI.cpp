#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <uvent/Uvent.h>
#include <uvent/system/SystemContext.h>

#include <unet/core/acceptor.hpp>
#include <unet/core/streams/plaintext.hpp>
#include <unet/http/client.hpp>

namespace {

struct CliOptions {
    std::string host{"127.0.0.1"};
    std::uint16_t port{22813};
    std::string tenant{"sandbox"};
    std::string api_key{};
    std::string content_type{"application/json"};
    std::string body{};
    std::string method{"GET"};
    std::string path{"/health"};
};

void print_usage() {
    std::cout << "Usage:\n";
    std::cout << "  HttpClientCli [--host H] [--port P] [--tenant T] [--api-key K] [--body B] [--content-type C] [METHOD] [PATH]\n";
    std::cout << "Examples:\n";
    std::cout << "  HttpClientCli GET /health\n";
    std::cout << "  HttpClientCli --api-key demo-local-key --body '{\"msg\":\"hello\"}' POST /v1/tasks\n";
}

std::optional<CliOptions> parse_args(int argc, char **argv) {
    CliOptions opt;
    std::vector<std::string> positional;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto require_value = [&](std::string &target) -> bool {
            if (i + 1 >= argc) {
                std::cerr << "missing value for " << arg << '\n';
                return false;
            }
            target = argv[++i];
            return true;
        };

        if (arg == "--help" || arg == "-h") {
            print_usage();
            return std::nullopt;
        }
        if (arg == "--host") {
            if (!require_value(opt.host)) {
                return std::nullopt;
            }
            continue;
        }
        if (arg == "--port") {
            std::string port_text;
            if (!require_value(port_text)) {
                return std::nullopt;
            }
            const auto p = std::strtoul(port_text.c_str(), nullptr, 10);
            if (p > 65535) {
                std::cerr << "port out of range: " << p << '\n';
                return std::nullopt;
            }
            opt.port = static_cast<std::uint16_t>(p);
            continue;
        }
        if (arg == "--tenant") {
            if (!require_value(opt.tenant)) {
                return std::nullopt;
            }
            continue;
        }
        if (arg == "--api-key") {
            if (!require_value(opt.api_key)) {
                return std::nullopt;
            }
            continue;
        }
        if (arg == "--body") {
            if (!require_value(opt.body)) {
                return std::nullopt;
            }
            continue;
        }
        if (arg == "--content-type") {
            if (!require_value(opt.content_type)) {
                return std::nullopt;
            }
            continue;
        }

        positional.push_back(arg);
    }

    if (!positional.empty()) {
        opt.method = positional[0];
    }
    if (positional.size() >= 2) {
        opt.path = positional[1];
    }

    for (char &c : opt.method) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    if (opt.path.empty() || opt.path.front() != '/') {
        opt.path = "/" + opt.path;
    }

    return opt;
}

usub::uvent::task::Awaitable<void> run_request(usub::Uvent &runtime, CliOptions opt) {
    using Client = usub::unet::http::ClientImpl<usub::unet::core::stream::PlainText>;
    using VERSION = usub::unet::http::VERSION;

    Client client{};

    usub::unet::http::Request req{};
    req.metadata.method_token = opt.method;
    req.metadata.version = VERSION::HTTP_1_1;
    req.metadata.uri.scheme = "http";
    req.metadata.uri.authority.host = opt.host;
    req.metadata.uri.authority.port = opt.port;
    req.metadata.authority = opt.host + ":" + std::to_string(opt.port);
    req.metadata.uri.path = opt.path;

    req.headers.addHeader("x-tenant", opt.tenant);
    if (!opt.api_key.empty()) {
        req.headers.addHeader("x-api-key", opt.api_key);
    }
    if (!opt.body.empty()) {
        req.headers.addHeader("content-type", opt.content_type);
        req.body = opt.body;
    }

    auto result = co_await client.request(std::move(req));
    if (!result) {
        std::cerr << "request failed: " << result.error().message << '\n';
        runtime.stop();
        co_return;
    }

    std::cout << "status: " << result->metadata.status_code << '\n';
    for (const auto &h : result->headers.all()) {
        std::cout << h.key << ": " << h.value << '\n';
    }
    std::cout << "\n" << result->body << '\n';

    runtime.stop();
    co_return;
}

} // namespace

int main(int argc, char **argv) {
    auto options = parse_args(argc, argv);
    if (!options.has_value()) {
        if (argc == 1) {
            print_usage();
        }
        return argc > 1 ? 1 : 0;
    }

    usub::Uvent runtime{1};
    usub::uvent::system::co_spawn(run_request(runtime, *options));
    runtime.run();
    return 0;
}
