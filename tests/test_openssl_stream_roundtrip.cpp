#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include <uvent/Uvent.h>
#include <uvent/system/SystemContext.h>

#include "unet/core/streams/openssl.hpp"

namespace {
    constexpr int kUventThreads = 2;
    constexpr std::uint16_t kPort = 24443;
    constexpr std::string_view kClientMessage = "ping from client\n";
    constexpr std::string_view kServerMessage = "pong from server\n";

    // Self-signed cert for test-only localhost TLS.
    constexpr const char *kTestKeyPem = R"(-----BEGIN PRIVATE KEY-----
MIIEvwIBADANBgkqhkiG9w0BAQEFAASCBKkwggSlAgEAAoIBAQCpxqvxiyRDLXyX
/SjBmyGE/WiTHF7IEDEeK2HRwhz+gJnh4lPy+UwQlv2ranE9Ds6mnrZlEEGVrWft
oEcoZMX2Jj7Xs5mXYuZba587d1fTswslyXSmiqwyKkzwqyf2b6RAY7TwcCn+yzyl
xtfWLmNSlIPCjqEz6cw+1gUrXwM5b9+de7bOOCwWQTbuEFynMAuXwq6uEkyc97Ue
Phx9EUKzBJCayL7Vvus9C9eL/R85KSC9N4gCuvMzuKgQPhu9GD8ELtQ4RSN7jOuC
YrIP3MlCPfEKNcehTnQhZsaaFCMJNmHRJa7BOn8RH1pnZ4ownYD5jkObIEzpg5bm
PrLW8ivjAgMBAAECggEATvr1knDAdd/7TlmfVDHu7gdVVtqj3T5rGzXjU8mR43PA
Iq0/kJEZKb6UU+I4u8bfq/O5tlpRqGf7KTie4dO/pDs2kPIkSaDguTbf5LIoUXvB
e9q0QaXRZaT4Fh4JgUN+jdXUnuYPI1E47YYLcsAxxIECqsffhl8FfVXjIcIZpeX5
ss1jwj3Ix6inKOmWkhfWzfVCHlTsAJgzi+MEr6/B2sh3eSQLrA/mhDFiEwdbYUO9
3bje9lvRFYgYB/38KreCsur/y4VBQq8vnQBPdegayMzOu6/yez/UpbY798WWUydS
J4joR0vsbS1DseWHKjlelCUzJ7Tez5CpqbdvII2hAQKBgQDveNgfNAoZw28f1Ogw
lsXGS9tQUdHbeqrWQ0oeqcWKPPXm4QXE+GpBCHY+1TTtLePZK7TKBbTroFX5LMqV
ugKeASwmu6wq9EyqlOkNHLMJV9zWH0BtX9fs+WHD6FYTW+8p+ZvP4PI1RQlH7oOR
lyxuwbzeUZYWHYXtdsvNiz8DUwKBgQC1fmQf9/EekhYluqMJo+AWF3lWRPNOfjnk
h972E+E8o2FVz9USlWyxBdUF+BAm9IGKuuaF+MSsiy4sb5fDM57R5N5hhn8z+lD1
7SPTYc1hMVGGUygEYWsb96FPbHkW7OlmETcdJQiCSAIMB9OSXNiljSoSOBMPVBb5
48jwPsMzMQKBgQDEwb6ZTLGr32azecRY/9h6CCOnR8KsmYo6R8ljjkwvO80zKNoL
r6vlySUWlSlKYvUdn4qIns26998Lv2CoStARsJbtMC/Sjy1azsT5MAZue9GH4N+X
vjL0kyZfx8rJVzUXgO4jjAiV+iYZAwD5I4OvcOFUrSYq/5DlvkDPnkRk3wKBgQCo
jXjvN5T2jzWCVJKVoVu7KJHHTMGpZBDf7E9kuZG0fwcmap23ZI5M2N29bWOSygCE
lo8AXMhKdferzPdkkcwtoh/k8sOvwgjuXA2pgmr6mJLd7Nx9NPwEPTXSTSsn313j
LJnCt2HvnHBKO/qBMzkAhtlLkkghNDciXCmA9MjUAQKBgQC5qHcbB0KzGofGBvMi
qGgYmp1jUlg51+gxnm54uqK/wUBqBRDQ1+S43bKNXECzClXqLGLUa6MLmepEqKGU
UcCfw0STnL5AcGqYvw5UOSckS25NcEu7DcCxCUj/cU0ecpiudNyO//pGKdJaiPxY
WvDghzma3lWrHr8agzWaYkzO1Q==
-----END PRIVATE KEY-----
)";

    constexpr const char *kTestCertPem = R"(-----BEGIN CERTIFICATE-----
MIIDCTCCAfGgAwIBAgIUFVu91uWwrcX/MgKQEhq/NvLBMzAwDQYJKoZIhvcNAQEL
BQAwFDESMBAGA1UEAwwJbG9jYWxob3N0MB4XDTI2MDIxMjIxMzUzOVoXDTI2MDIx
MzIxMzUzOVowFDESMBAGA1UEAwwJbG9jYWxob3N0MIIBIjANBgkqhkiG9w0BAQEF
AAOCAQ8AMIIBCgKCAQEAqcar8YskQy18l/0owZshhP1okxxeyBAxHith0cIc/oCZ
4eJT8vlMEJb9q2pxPQ7Opp62ZRBBla1n7aBHKGTF9iY+17OZl2LmW2ufO3dX07ML
Jcl0poqsMipM8Ksn9m+kQGO08HAp/ss8pcbX1i5jUpSDwo6hM+nMPtYFK18DOW/f
nXu2zjgsFkE27hBcpzALl8KurhJMnPe1Hj4cfRFCswSQmsi+1b7rPQvXi/0fOSkg
vTeIArrzM7ioED4bvRg/BC7UOEUje4zrgmKyD9zJQj3xCjXHoU50IWbGmhQjCTZh
0SWuwTp/ER9aZ2eKMJ2A+Y5DmyBM6YOW5j6y1vIr4wIDAQABo1MwUTAdBgNVHQ4E
FgQU2+A3lAm4s0bEV2eRuWbsfnxMIQowHwYDVR0jBBgwFoAU2+A3lAm4s0bEV2eR
uWbsfnxMIQowDwYDVR0TAQH/BAUwAwEB/zANBgkqhkiG9w0BAQsFAAOCAQEAk7Hj
EJy/p1adtk18/HwQI0MoIe55ilv/7bkMpbKhaTbo3k2s+4w7xx66kT3aLQGAfnRF
zFYGpHYfsVfHuFpMZfRUTs0Sx2UkLS7lmfwtcrfai0UpdIZ6JBz6zvRicZrAefJw
hMimGKUhwgVtsdEuxioHt5z9KptNhiN/GTfzAh9aKCeBiReCYlvxlwatJGTWRFvc
QZhMnKAy6yFCqXNvyCJVjvMrCwMVklKKYkEmIw9wU7U7P3y6S/wT4juKwQlqcuR7
2CqsCfbQMjD/pKoebmDyOGgZSGTyL7ciIkr6GEiB3/PMUK1FBFLthxZQMNHa1JKn
WUxhbPq2E7RCJ8fYPw==
-----END CERTIFICATE-----
)";

    struct SharedState {
        std::atomic<bool> server_done{false};
        std::atomic<bool> client_done{false};
        std::mutex error_mutex;
        std::string error{};

        void fail(std::string message) {
            std::lock_guard<std::mutex> lock(error_mutex);
            if (error.empty()) { error = std::move(message); }
        }

        void markServerDone() { server_done.store(true, std::memory_order_release); }
        void markClientDone() { client_done.store(true, std::memory_order_release); }

        bool success() const {
            return error.empty() && server_done.load(std::memory_order_acquire) &&
                   client_done.load(std::memory_order_acquire);
        }
    };

    bool write_file(const std::filesystem::path &path, const std::string_view text) {
        std::ofstream out(path, std::ios::binary);
        if (!out) { return false; }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        return out.good();
    }

    usub::uvent::task::Awaitable<std::optional<std::string>> read_line(usub::unet::core::stream::OpenSSLStream &stream,
                                                                       usub::uvent::net::TCPClientSocket socket) {
        usub::uvent::utils::DynamicBuffer buffer;
        std::string acc;
        for (int i = 0; i < 16; ++i) {
            const ssize_t n = co_await stream.read(socket, buffer);
            if (n <= 0) { co_return std::nullopt; }
            acc.append(reinterpret_cast<const char *>(buffer.data()), static_cast<std::size_t>(n));
            if (acc.find('\n') != std::string::npos) { co_return acc; }
        }
        co_return std::nullopt;
    }

    usub::uvent::task::Awaitable<void> server_task(SharedState &state, std::string key_path, std::string cert_path) {
        std::string ip = "127.0.0.1";
        usub::uvent::net::TCPServerSocket server_socket{ip, kPort, 8, usub::uvent::utils::net::IPV::IPV4,
                                                        usub::uvent::utils::net::TCP};
        usub::unet::core::stream::OpenSSLStream tls_server{key_path, cert_path};

        auto accepted = co_await server_socket.async_accept();
        if (!accepted) {
            state.fail("server accept failed");
            co_return;
        }

        auto peer = std::move(*accepted);
        auto inbound = co_await read_line(tls_server, peer);
        if (!inbound.has_value()) {
            state.fail("server failed to read tls payload");
            co_return;
        }
        if (*inbound != kClientMessage) {
            state.fail("server received unexpected payload: " + *inbound);
            co_return;
        }

        co_await tls_server.send(peer, kServerMessage);
        co_await tls_server.shutdown(peer);
        state.markServerDone();
        co_return;
    }

    usub::uvent::task::Awaitable<void> client_task(SharedState &state) {
        co_await usub::uvent::system::this_coroutine::sleep_for(std::chrono::milliseconds(50));

        usub::unet::core::stream::OpenSSLStream::Config cfg{};
        cfg.mode = usub::unet::core::stream::OpenSSLStream::MODE::CLIENT;
        cfg.verify_peer = false;// self-signed cert in test
        cfg.server_name = "localhost";
        usub::unet::core::stream::OpenSSLStream tls_client{cfg};

        usub::uvent::net::TCPClientSocket socket;
        std::string host = "127.0.0.1";
        std::string port = std::to_string(kPort);
        auto connect_err = co_await socket.async_connect(host, port, std::chrono::milliseconds(3000));
        if (connect_err.has_value()) {
            state.fail("client connect failed");
            co_return;
        }

        co_await tls_client.send(socket, kClientMessage);
        auto inbound = co_await read_line(tls_client, socket);
        if (!inbound.has_value()) {
            state.fail("client failed to read tls payload");
            co_return;
        }
        if (*inbound != kServerMessage) {
            state.fail("client received unexpected payload: " + *inbound);
            co_return;
        }

        co_await tls_client.shutdown(socket);
        state.markClientDone();
        co_return;
    }

}// namespace

int main() {
    const auto tmp_dir = std::filesystem::temp_directory_path() / "unet_openssl_stream_roundtrip";
    std::error_code ec;
    std::filesystem::create_directories(tmp_dir, ec);

    const auto key_path = tmp_dir / "key.pem";
    const auto cert_path = tmp_dir / "cert.pem";

    if (!write_file(key_path, kTestKeyPem) || !write_file(cert_path, kTestCertPem)) {
        std::cerr << "failed to prepare tls test cert files\n";
        return 2;
    }
    usub::Uvent uvent{kUventThreads};
    SharedState state{};
    usub::uvent::system::co_spawn(server_task(state, key_path.string(), cert_path.string()));
    usub::uvent::system::co_spawn(client_task(state));
    std::jthread run_thread([&uvent]() { uvent.run(); });

    // Stop from main thread only, after a fixed wait.
    std::this_thread::sleep_for(std::chrono::seconds(5));
    uvent.stop();
    // if (run_thread.joinable()) { run_thread.join(); }

    std::filesystem::remove(key_path, ec);
    std::filesystem::remove(cert_path, ec);
    std::filesystem::remove(tmp_dir, ec);

    if (!state.success()) {
        if (state.error.empty()) {
            std::cerr << "openssl stream roundtrip failed: timeout waiting for completion\n";
        } else {
            std::cerr << "openssl stream roundtrip failed: " << state.error << "\n";
        }
        return 1;
    }

    std::cout << "openssl stream roundtrip passed\n";
    return 0;
}
