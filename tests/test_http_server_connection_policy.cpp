#include <chrono>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include <uvent/Uvent.h>
#include <uvent/system/SystemContext.h>

#include "unet/core/config.hpp"
#include "unet/http.hpp"
#include "http_client_test_support.hpp"

namespace {
    using namespace std::string_view_literals;

    constexpr int kUventThreads = 4;
    constexpr std::uint16_t kPort = 24885;

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

    usub::uvent::task::Awaitable<std::optional<usub::uvent::net::TCPClientSocket>>
    connect_socket() {
        usub::uvent::net::TCPClientSocket socket{};
        auto connect_error = co_await socket.async_connect("127.0.0.1", std::to_string(kPort),
                                                           std::chrono::milliseconds{3000});
        if (connect_error.has_value()) { co_return std::nullopt; }
        co_return socket;
    }

    bool response_has_close(const usub::unet::http::Response &response) {
        const auto header = response.headers.value("connection");
        return header.has_value() && *header == "close";
    }

    bool response_has_keep_alive_timeout(const usub::unet::http::Response &response, std::string_view value) {
        const auto header = response.headers.value("keep-alive");
        return header.has_value() && *header == value;
    }

    usub::uvent::task::Awaitable<std::optional<usub::unet::http::Response>>
    send_request(usub::uvent::net::TCPClientSocket socket, std::string_view request) {
        if (!co_await http_client_test_support::write_all(socket, request)) { co_return std::nullopt; }
        co_return co_await http_client_test_support::read_http_response(socket);
    }

    usub::uvent::task::Awaitable<int> run_checks() {
        co_await usub::uvent::system::this_coroutine::sleep_for(std::chrono::milliseconds(50));

        auto keep_alive_socket = co_await connect_socket();
        if (!keep_alive_socket.has_value()) { co_return 10; }

        const auto keep_alive_first = co_await send_request(
                *keep_alive_socket,
                "GET /policy HTTP/1.1\r\nHost: 127.0.0.1:24885\r\n\r\n"sv);
        if (!keep_alive_first || keep_alive_first->metadata.status_code != 200 || response_has_close(*keep_alive_first)) {
            co_return 11;
        }

        const auto keep_alive_second = co_await send_request(
                *keep_alive_socket,
                "GET /policy HTTP/1.1\r\nHost: 127.0.0.1:24885\r\nConnection: close\r\n\r\n"sv);
        if (!keep_alive_second || keep_alive_second->metadata.status_code != 200 || !response_has_close(*keep_alive_second)) {
            co_return 12;
        }

        const auto keep_alive_third = co_await send_request(
                *keep_alive_socket,
                "GET /policy HTTP/1.1\r\nHost: 127.0.0.1:24885\r\n\r\n"sv);
        if (keep_alive_third.has_value()) { co_return 13; }

        auto http10_socket = co_await connect_socket();
        if (!http10_socket.has_value()) { co_return 20; }

        const auto http10_response = co_await send_request(
                *http10_socket,
                "GET /policy HTTP/1.0\r\nHost: 127.0.0.1:24885\r\n\r\n"sv);
        if (!http10_response || http10_response->metadata.status_code != 200 || !response_has_close(*http10_response)) {
            co_return 21;
        }

        const auto http10_second = co_await send_request(
                *http10_socket,
                "GET /policy HTTP/1.0\r\nHost: 127.0.0.1:24885\r\n\r\n"sv);
        if (http10_second.has_value()) { co_return 22; }

        auto timeout_socket = co_await connect_socket();
        if (!timeout_socket.has_value()) { co_return 30; }

        const auto timeout_response = co_await send_request(
                *timeout_socket,
                "GET /policy HTTP/1.1\r\nHost: 127.0.0.1:24885\r\nConnection: keep-alive\r\nKeep-Alive: timeout=1\r\n\r\n"sv);
        if (!timeout_response || timeout_response->metadata.status_code != 200 ||
            !response_has_keep_alive_timeout(*timeout_response, "timeout=1"sv)) {
            co_return 31;
        }

        co_await usub::uvent::system::this_coroutine::sleep_for(std::chrono::milliseconds(1200));

        const auto timeout_second = co_await send_request(
                *timeout_socket,
                "GET /policy HTTP/1.1\r\nHost: 127.0.0.1:24885\r\n\r\n"sv);
        if (timeout_second.has_value()) { co_return 32; }

        co_return 0;
    }
}// namespace

int main() {
    usub::Uvent uvent{kUventThreads};
    usub::unet::http::ServerRadix server{uvent, make_plain_config(kPort)};
    server.handle("GET", "/policy",
                  [](usub::unet::http::Request &, usub::unet::http::Response &response)
                          -> usub::uvent::task::Awaitable<void> {
                      response.setStatus(200).setBody("ok");
                      co_return;
                  });

    int result_code = -1;
    usub::uvent::system::co_spawn([&]() -> usub::uvent::task::Awaitable<void> {
        result_code = co_await run_checks();
        co_return;
    }());

    std::jthread run_thread([&uvent]() { uvent.run(); });
    std::this_thread::sleep_for(std::chrono::seconds(6));
    uvent.stop();

    if (result_code != 0) {
        std::cerr << "http server connection policy test failed: code=" << result_code << '\n';
        return 1;
    }

    std::cout << "http server connection policy test passed\n";
    return 0;
}
