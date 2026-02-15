#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include <uvent/Uvent.h>

#include "unet/core/acceptor.hpp"
#include "unet/core/streams/openssl.hpp"
#include "unet/core/streams/plaintext.hpp"
#include "unet/http/client.hpp"

namespace {
    using namespace std::string_view_literals;

    using MixedClient =
            usub::unet::http::ClientImpl<usub::unet::core::stream::PlainText, usub::unet::core::stream::OpenSSLStream>;
    using usub::unet::http::ClientRequestOptions;
    using usub::unet::http::Request;
    using usub::unet::http::VERSION;

    struct SharedState {
        std::atomic<bool> finished{false};
        std::mutex mutex{};
        std::string error{};
        std::optional<std::uint16_t> plain_status{};
        std::optional<std::uint16_t> tls_status{};
        std::size_t plain_body_size{0};
        std::size_t tls_body_size{0};

        void fail(std::string message) {
            std::lock_guard<std::mutex> lock(this->mutex);
            if (this->error.empty()) { this->error = std::move(message); }
        }

        void done(std::uint16_t plain_code, std::size_t plain_size, std::uint16_t tls_code, std::size_t tls_size) {
            std::lock_guard<std::mutex> lock(this->mutex);
            this->plain_status = plain_code;
            this->tls_status = tls_code;
            this->plain_body_size = plain_size;
            this->tls_body_size = tls_size;
            this->finished.store(true, std::memory_order_release);
        }
    };

    Request build_get_request(std::string_view scheme, std::string_view host, std::uint16_t port) {
        Request request{};
        request.metadata.method_token = "GET";
        request.metadata.version = VERSION::HTTP_1_1;
        request.metadata.uri.scheme = std::string(scheme);
        request.metadata.uri.authority.host = std::string(host);
        request.metadata.uri.authority.port = port;
        request.metadata.authority = std::string(host);
        request.metadata.uri.path = "/";
        request.headers.addHeader("user-agent"sv, "unet-test/1.0"sv);
        request.headers.addHeader("accept"sv, "*/*"sv);
        return request;
    }

    usub::uvent::task::Awaitable<void> run_example_requests(SharedState &state) {
        MixedClient client{};

        ClientRequestOptions plain_options{};
        plain_options.connect_timeout = std::chrono::milliseconds{4000};

        auto plain_result = co_await client.request(build_get_request("http", "example.com", 80), plain_options);
        if (!plain_result) {
            state.fail("plain client request failed: " + plain_result.error().message);
            co_return;
        }
        if (plain_result->metadata.status_code < 200 || plain_result->metadata.status_code >= 400) {
            state.fail("plain client status out of expected range: " +
                       std::to_string(plain_result->metadata.status_code));
            co_return;
        }
        if (plain_result->body.empty()) {
            state.fail("plain client body is empty");
            co_return;
        }

        usub::unet::core::stream::OpenSSLStream::Config tls_cfg{};
        tls_cfg.mode = usub::unet::core::stream::OpenSSLStream::MODE::CLIENT;
        tls_cfg.verify_peer = true;
        tls_cfg.server_name = "example.com";
        client.stream<usub::unet::core::stream::OpenSSLStream>().setConfig(std::move(tls_cfg));

        ClientRequestOptions tls_options{};
        tls_options.connect_timeout = std::chrono::milliseconds{4000};

        auto tls_result = co_await client.request(build_get_request("https", "example.com", 443), tls_options);
        if (!tls_result) {
            state.fail("tls client request failed: " + tls_result.error().message);
            co_return;
        }
        if (tls_result->metadata.status_code < 200 || tls_result->metadata.status_code >= 400) {
            state.fail("tls client status out of expected range: " + std::to_string(tls_result->metadata.status_code));
            co_return;
        }
        if (tls_result->body.empty()) {
            state.fail("tls client body is empty");
            co_return;
        }

        state.done(plain_result->metadata.status_code, plain_result->body.size(), tls_result->metadata.status_code,
                   tls_result->body.size());
        co_return;
    }
}// namespace

int main() {
    SharedState state{};
    usub::Uvent uvent{2};

    usub::uvent::system::co_spawn(run_example_requests(state));

    std::jthread run_thread([&uvent]() { uvent.run(); });
    std::this_thread::sleep_for(std::chrono::seconds(10));
    uvent.stop();

    if (!state.error.empty()) {
        std::cerr << "http client example test failed: " << state.error << '\n';
        return 1;
    }

    if (!state.finished.load(std::memory_order_acquire) || !state.plain_status.has_value() ||
        !state.tls_status.has_value()) {
        std::cerr << "http client example test failed: timeout waiting for plain+tls responses\n";
        return 1;
    }

    std::cout << "http client example test passed: plain_status=" << *state.plain_status
              << ", plain_body_size=" << state.plain_body_size << ", tls_status=" << *state.tls_status
              << ", tls_body_size=" << state.tls_body_size << '\n';
    return 0;
}
