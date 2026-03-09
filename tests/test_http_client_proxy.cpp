#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

#include <uvent/Uvent.h>
#include <uvent/system/SystemContext.h>

#include "unet/core/config.hpp"
#include "unet/http.hpp"
#include "unet/core/streams/plaintext.hpp"
#include "unet/http/client.hpp"
#include "unet/http/v1/wire/response_serializer.hpp"
#include "http_client_test_support.hpp"

namespace {
    using namespace std::string_view_literals;

    constexpr int kUventThreads = 4;
    constexpr std::uint16_t kProxyPort = 24881;
    constexpr std::uint16_t kPlainOriginPort = 24883;
    constexpr std::string_view kProxyUser = "proxy-user";
    constexpr std::string_view kProxyPass = "proxy-pass";
    constexpr std::string_view kExpectedProxyAuth = "Basic cHJveHktdXNlcjpwcm94eS1wYXNz";

    using ProxyClient = usub::unet::http::ClientImpl<usub::unet::core::stream::PlainText>;

    usub::unet::core::Config make_plain_config(std::uint16_t port) {
        usub::unet::core::Config config{};
        usub::unet::core::Config::Object section{};

        usub::unet::core::Config::Value host{};
        host.data = std::string{"127.0.0.1"};
        section.emplace("host", std::move(host));

        usub::unet::core::Config::Value port_value{};
        port_value.data = static_cast<std::uint64_t>(port);
        section.emplace("port", std::move(port_value));

        usub::unet::core::Config::Value section_value{};
        section_value.data = std::move(section);
        config.root.emplace("HTTP.PlainTextStream", std::move(section_value));
        return config;
    }

    struct SharedState {
        std::atomic<bool> proxy_done{false};
        std::atomic<bool> origin_done{false};
        std::atomic<bool> client_done{false};
        std::mutex mutex{};
        std::string error{};

        void fail(std::string message) {
            std::lock_guard<std::mutex> lock(this->mutex);
            if (this->error.empty()) { this->error = std::move(message); }
        }

        bool success() const {
            return this->error.empty() && this->proxy_done.load(std::memory_order_acquire) &&
                   this->origin_done.load(std::memory_order_acquire) &&
                   this->client_done.load(std::memory_order_acquire);
        }
    };

    usub::uvent::task::Awaitable<void> proxy_server(SharedState &state) {
        usub::uvent::net::TCPServerSocket server_socket{"127.0.0.1", kProxyPort, 8,
                                                        usub::uvent::utils::net::IPV::IPV4,
                                                        usub::uvent::utils::net::TCP};

        auto accepted = co_await server_socket.async_accept();
        if (!accepted) {
            state.fail("proxy accept failed");
            co_return;
        }

        auto client_socket = std::move(*accepted);
        std::string pending{};
        auto headers = co_await http_client_test_support::read_http_headers(client_socket, pending);
        if (!headers.has_value()) {
            state.fail("proxy failed to read request");
            co_return;
        }

        const auto proxy_auth = http_client_test_support::find_header(*headers, "Proxy-Authorization");
        if (!proxy_auth.has_value() || *proxy_auth != kExpectedProxyAuth) {
            state.fail("proxy authorization header missing or incorrect");
            co_return;
        }

        if (!http_client_test_support::starts_with(*headers,
                                                   "GET http://127.0.0.1:24883/plain?via=proxy HTTP/1.1\r\n"sv)) {
            state.fail("proxy received unexpected request line");
            co_return;
        }

        usub::uvent::net::TCPClientSocket upstream{};
        auto connect_error = co_await upstream.async_connect("127.0.0.1", std::to_string(kPlainOriginPort),
                                                             std::chrono::milliseconds{3000});
        if (connect_error.has_value()) {
            state.fail("proxy failed to connect to plain origin");
            client_socket.shutdown();
            co_return;
        }

        const std::string translated_request =
                "GET /plain?via=proxy HTTP/1.1\r\n"
                "Host: 127.0.0.1:24883\r\n"
                "Accept: */*\r\n"
                "Connection: close\r\n"
                "\r\n";

        if (!co_await http_client_test_support::write_all(upstream, translated_request)) {
            state.fail("proxy failed to write translated origin request");
            upstream.shutdown();
            client_socket.shutdown();
            co_return;
        }

        auto upstream_response = co_await http_client_test_support::read_http_response(upstream);
        if (!upstream_response.has_value()) {
            state.fail("proxy failed to read origin response");
            upstream.shutdown();
            client_socket.shutdown();
            co_return;
        }

        const std::string serialized =
                usub::unet::http::v1::ResponseSerializer::serialize(*upstream_response);
        if (!co_await http_client_test_support::write_all(client_socket, serialized)) {
            state.fail("proxy failed to forward origin response");
            upstream.shutdown();
            client_socket.shutdown();
            co_return;
        }

        upstream.shutdown();
        client_socket.shutdown();
        state.proxy_done.store(true, std::memory_order_release);
        co_return;
    }

    usub::uvent::task::Awaitable<void> client_task(SharedState &state) {
        co_await usub::uvent::system::this_coroutine::sleep_for(std::chrono::milliseconds(50));

        ProxyClient client{};
        usub::unet::http::ClientRequestOptions options{};
        options.connect_timeout = std::chrono::milliseconds{3000};
        options.proxy = usub::unet::http::ClientProxyOptions{
                .host = "127.0.0.1",
                .port = kProxyPort,
                .username = std::string(kProxyUser),
                .password = std::string(kProxyPass),
        };

        usub::unet::http::Request request{};
        request.metadata.method_token = "GET";
        request.metadata.version = usub::unet::http::VERSION::HTTP_1_1;
        request.metadata.uri.scheme = "http";
        request.metadata.uri.authority.host = "127.0.0.1";
        request.metadata.uri.authority.port = kPlainOriginPort;
        request.metadata.authority = "127.0.0.1:24883";
        request.metadata.uri.path = "/plain";
        request.metadata.uri.query = "via=proxy";
        request.headers.addHeader("accept"sv, "*/*"sv);

        const auto result = co_await client.request(std::move(request), options);
        if (!result || result->metadata.status_code != 200 || result->body != "proxy-http-ok") {
            if (!result) {
                state.fail("plain proxied request failed: " + result.error().message);
            } else {
                state.fail("plain proxied request failed: status=" + std::to_string(result->metadata.status_code) +
                           " body=" + result->body);
            }
            co_return;
        }

        co_await client.close();
        state.client_done.store(true, std::memory_order_release);
        co_return;
    }
}// namespace

int main() {
    usub::Uvent uvent{kUventThreads};
    SharedState state{};

    usub::unet::http::ServerRadix plain_origin{uvent, make_plain_config(kPlainOriginPort)};
    plain_origin.handle("GET", "/plain",
                        [&state](usub::unet::http::Request &, usub::unet::http::Response &response)
                                -> usub::uvent::task::Awaitable<void> {
                            state.origin_done.store(true, std::memory_order_release);
                            response.setStatus(200)
                                    .addHeader("Connection", "close")
                                    .setBody("proxy-http-ok");
                            co_return;
                        });

    usub::uvent::system::co_spawn(proxy_server(state));
    usub::uvent::system::co_spawn(client_task(state));

    std::jthread run_thread([&uvent]() { uvent.run(); });
    std::this_thread::sleep_for(std::chrono::seconds(5));
    uvent.stop();

    if (!state.success()) {
        if (state.error.empty()) {
            std::cerr << "http client proxy test failed: timeout waiting for completion"
                      << " proxy_done=" << state.proxy_done.load(std::memory_order_acquire)
                      << " origin_done=" << state.origin_done.load(std::memory_order_acquire)
                      << " client_done=" << state.client_done.load(std::memory_order_acquire) << '\n';
        } else {
            std::cerr << "http client proxy test failed: " << state.error
                      << " proxy_done=" << state.proxy_done.load(std::memory_order_acquire)
                      << " origin_done=" << state.origin_done.load(std::memory_order_acquire)
                      << " client_done=" << state.client_done.load(std::memory_order_acquire) << '\n';
        }
        return 1;
    }

    std::cout << "http client proxy test passed\n";
    return 0;
}
