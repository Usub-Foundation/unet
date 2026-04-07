#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <uvent/Uvent.h>
#include <uvent/system/SystemContext.h>

#include "unet/core/config.hpp"
#include "unet/http.hpp"
#include "unet/http/client.hpp"
#include "http_client_test_support.hpp"

namespace {
    using namespace std::string_view_literals;

    constexpr int kUventThreads = 4;
    constexpr std::uint16_t kFrontendPort = 24880;
    constexpr std::uint16_t kOriginPort = 24884;

    using PlainClient = usub::unet::http::ClientImpl<usub::unet::core::stream::PlainText>;
    using Request = usub::unet::http::Request;
    using VERSION = usub::unet::http::VERSION;

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

    std::string_view request_target_from_headers(std::string_view headers) {
        const std::size_t line_end = headers.find("\r\n");
        if (line_end == std::string_view::npos) { return {}; }

        const std::string_view request_line = headers.substr(0, line_end);
        const std::size_t first_space = request_line.find(' ');
        if (first_space == std::string_view::npos) { return {}; }

        const std::size_t second_space = request_line.find(' ', first_space + 1);
        if (second_space == std::string_view::npos || second_space <= first_space + 1) { return {}; }

        return request_line.substr(first_space + 1, second_space - first_space - 1);
    }

    struct SharedState {
        std::atomic<int> accepted_connections{0};
        std::atomic<int> origin_requests{0};
        std::atomic<bool> client_done{false};
        std::mutex mutex{};
        std::string error{};
        std::vector<int> request_connection_ids{};

        void fail(std::string message) {
            std::lock_guard<std::mutex> lock(this->mutex);
            if (this->error.empty()) { this->error = std::move(message); }
        }

        void record_frontend_request(int connection_id, std::string_view headers) {
            static constexpr std::string_view expected_targets[] = {"/first", "/second", "/third"};

            const std::string_view target = request_target_from_headers(headers);
            std::lock_guard<std::mutex> lock(this->mutex);
            if (this->request_connection_ids.size() >= std::size(expected_targets)) {
                if (this->error.empty()) { this->error = "frontend relay observed too many requests"; }
                return;
            }

            if (target != expected_targets[this->request_connection_ids.size()]) {
                if (this->error.empty()) {
                    this->error = "frontend relay observed unexpected request target: " + std::string(target);
                }
                return;
            }

            this->request_connection_ids.push_back(connection_id);
        }

        bool success() const {
            return this->error.empty() && this->client_done.load(std::memory_order_acquire) &&
                   this->origin_requests.load(std::memory_order_acquire) == 3;
        }
    };

    usub::uvent::task::Awaitable<void> relay_origin_to_client(usub::uvent::net::TCPClientSocket origin,
                                                               usub::uvent::net::TCPClientSocket client) {
        usub::uvent::utils::DynamicBuffer buffer{};
        buffer.reserve(16 * 1024);

        for (;;) {
            buffer.clear();
            const ssize_t read_size = co_await origin.async_read(buffer, static_cast<std::size_t>(16 * 1024));
            if (read_size <= 0) {
                client.shutdown();
                break;
            }

            std::size_t offset = 0;
            while (offset < static_cast<std::size_t>(read_size)) {
                const ssize_t written = co_await client.async_write(reinterpret_cast<uint8_t *>(buffer.data() + offset),
                                                                    static_cast<std::size_t>(read_size) - offset);
                if (written <= 0) {
                    client.shutdown();
                    co_return;
                }
                offset += static_cast<std::size_t>(written);
            }
        }

        co_return;
    }

    usub::uvent::task::Awaitable<void> relay_client_to_origin(SharedState &state, int connection_id,
                                                               usub::uvent::net::TCPClientSocket client,
                                                               usub::uvent::net::TCPClientSocket origin) {
        usub::uvent::utils::DynamicBuffer buffer{};
        buffer.reserve(16 * 1024);
        std::string pending{};

        for (;;) {
            buffer.clear();
            const ssize_t read_size = co_await client.async_read(buffer, static_cast<std::size_t>(16 * 1024));
            if (read_size <= 0) {
                origin.shutdown();
                break;
            }

            const std::string_view chunk{reinterpret_cast<const char *>(buffer.data()), static_cast<std::size_t>(read_size)};
            pending.append(chunk.data(), chunk.size());

            for (;;) {
                const std::size_t end_of_headers = pending.find("\r\n\r\n");
                if (end_of_headers == std::string::npos) { break; }

                state.record_frontend_request(connection_id, std::string_view{pending.data(), end_of_headers + 4});
                pending.erase(0, end_of_headers + 4);
            }

            if (!co_await http_client_test_support::write_all(origin, chunk)) {
                state.fail("frontend relay failed to forward request bytes");
                origin.shutdown();
                break;
            }
        }

        co_return;
    }

    usub::uvent::task::Awaitable<void> frontend_connection(SharedState &state, int connection_id,
                                                            usub::uvent::net::TCPClientSocket client_socket) {
        usub::uvent::net::TCPClientSocket origin_socket{};
        auto connect_error = co_await origin_socket.async_connect("127.0.0.1", std::to_string(kOriginPort),
                                                                  std::chrono::milliseconds{3000});
        if (connect_error.has_value()) {
            state.fail("frontend relay failed to connect to origin server");
            client_socket.shutdown();
            co_return;
        }

        usub::uvent::system::co_spawn(relay_origin_to_client(origin_socket, client_socket));
        co_await relay_client_to_origin(state, connection_id, client_socket, origin_socket);
    }

    usub::uvent::task::Awaitable<void> frontend_server(SharedState &state) {
        usub::uvent::net::TCPServerSocket server_socket{"127.0.0.1", kFrontendPort, 8,
                                                        usub::uvent::utils::net::IPV::IPV4,
                                                        usub::uvent::utils::net::TCP};

        int connection_id = 0;
        while (state.error.empty() && state.accepted_connections.load(std::memory_order_acquire) < 2) {
            auto accepted = co_await server_socket.async_accept();
            if (!accepted) { continue; }

            ++connection_id;
            state.accepted_connections.fetch_add(1, std::memory_order_acq_rel);
            usub::uvent::system::co_spawn(frontend_connection(state, connection_id, std::move(*accepted)));
        }

        co_return;
    }

    usub::uvent::task::Awaitable<void> client_task(SharedState &state) {
        co_await usub::uvent::system::this_coroutine::sleep_for(std::chrono::milliseconds(50));

        PlainClient client{};
        usub::unet::http::ClientRequestOptions options{};
        options.connect_timeout = std::chrono::milliseconds{2000};

        Request first_request{};
        first_request.metadata.method_token = "GET";
        first_request.metadata.version = VERSION::HTTP_1_1;
        first_request.metadata.uri.scheme = "http";
        first_request.metadata.uri.authority.host = "127.0.0.1";
        first_request.metadata.uri.authority.port = kFrontendPort;
        first_request.metadata.authority = "127.0.0.1";
        first_request.metadata.uri.path = "/first";
        first_request.headers.addHeader("user-agent"sv, "unet-persistence-test/1.0"sv);
        first_request.headers.addHeader("accept"sv, "*/*"sv);

        const auto first = co_await client.request(std::move(first_request), options);
        if (!first || first->metadata.status_code != 200 || first->body != "response-1") {
            state.fail("first client request failed");
            co_return;
        }

        Request second_request{};
        second_request.metadata.method_token = "GET";
        second_request.metadata.version = VERSION::HTTP_1_1;
        second_request.metadata.uri.scheme = "http";
        second_request.metadata.uri.authority.host = "127.0.0.1";
        second_request.metadata.uri.authority.port = kFrontendPort;
        second_request.metadata.authority = "127.0.0.1";
        second_request.metadata.uri.path = "/second";
        second_request.headers.addHeader("user-agent"sv, "unet-persistence-test/1.0"sv);
        second_request.headers.addHeader("accept"sv, "*/*"sv);

        const auto second = co_await client.request(std::move(second_request), options);
        if (!second || second->metadata.status_code != 200 || second->body != "response-2") {
            state.fail("second client request failed");
            co_return;
        }

        co_await usub::uvent::system::this_coroutine::sleep_for(std::chrono::milliseconds(1200));

        Request third_request{};
        third_request.metadata.method_token = "GET";
        third_request.metadata.version = VERSION::HTTP_1_1;
        third_request.metadata.uri.scheme = "http";
        third_request.metadata.uri.authority.host = "127.0.0.1";
        third_request.metadata.uri.authority.port = kFrontendPort;
        third_request.metadata.authority = "127.0.0.1";
        third_request.metadata.uri.path = "/third";
        third_request.headers.addHeader("user-agent"sv, "unet-persistence-test/1.0"sv);
        third_request.headers.addHeader("accept"sv, "*/*"sv);

        const auto third = co_await client.request(std::move(third_request), options);
        if (!third || third->metadata.status_code != 200 || third->body != "response-3") {
            state.fail("third client request failed");
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

    usub::unet::http::ServerRadix origin_server{uvent, make_plain_config(kOriginPort)};
    origin_server.handle("GET", "/first", [&state](usub::unet::http::Request &, usub::unet::http::Response &response)
                                      -> usub::uvent::task::Awaitable<void> {
        state.origin_requests.fetch_add(1, std::memory_order_acq_rel);
        response.setStatus(200)
                .addHeader("Connection", "keep-alive")
                .addHeader("Keep-Alive", "timeout=1, max=10")
                .setBody("response-1");
        co_return;
    });
    origin_server.handle("GET", "/second", [&state](usub::unet::http::Request &, usub::unet::http::Response &response)
                                       -> usub::uvent::task::Awaitable<void> {
        state.origin_requests.fetch_add(1, std::memory_order_acq_rel);
        response.setStatus(200)
                .addHeader("Connection", "keep-alive")
                .addHeader("Keep-Alive", "timeout=1, max=10")
                .setBody("response-2");
        co_return;
    });
    origin_server.handle("GET", "/third", [&state](usub::unet::http::Request &, usub::unet::http::Response &response)
                                      -> usub::uvent::task::Awaitable<void> {
        state.origin_requests.fetch_add(1, std::memory_order_acq_rel);
        response.setStatus(200)
                .addHeader("Connection", "close")
                .setBody("response-3");
        co_return;
    });

    usub::uvent::system::co_spawn(frontend_server(state));
    usub::uvent::system::co_spawn(client_task(state));

    std::jthread run_thread([&uvent]() { uvent.run(); });
    std::this_thread::sleep_for(std::chrono::seconds(5));
    uvent.stop();

    if (!state.success()) {
        if (state.error.empty()) {
            std::cerr << "http client persistence test failed: timeout waiting for completion\n";
        } else {
            std::cerr << "http client persistence test failed: " << state.error << '\n';
        }
        return 1;
    }

    const std::vector<int> expected_ids{1, 1, 2};
    if (state.accepted_connections.load(std::memory_order_acquire) != 2 ||
        state.request_connection_ids != expected_ids) {
        std::cerr << "http client persistence test failed: expected connections=2 and request ids [1,1,2], got "
                  << "connections=" << state.accepted_connections.load(std::memory_order_acquire) << " ids=";
        for (const int id: state.request_connection_ids) { std::cerr << id << ' '; }
        std::cerr << '\n';
        return 1;
    }

    std::cout << "http client persistence test passed\n";
    return 0;
}
