// WebSocket server integration test.
// Starts a real ServerRadix with a /ws echo handler, connects via a raw OS
// socket, performs the opening handshake, exchanges frames, checks close.
//
// Build with: cmake -DUNET_TESTS=ON, then: ./test_ws_server

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

// Platform socket headers
#ifdef _WIN32
#    include <winsock2.h>
#    include <ws2tcpip.h>
#    pragma comment(lib, "ws2_32.lib")
using SockFd = SOCKET;
static constexpr SockFd BAD_FD = INVALID_SOCKET;
#else
#    include <arpa/inet.h>
#    include <netinet/in.h>
#    include <sys/socket.h>
#    include <unistd.h>
using SockFd = int;
static constexpr SockFd BAD_FD = -1;
#endif

#include <uvent/Uvent.h>

#include "unet/http.hpp"
#include "unet/ws.hpp"

using namespace usub::unet::ws;

// ─── socket helpers ──────────────────────────────────────────────────────────

static void sock_init() {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
}

static void sock_close(SockFd fd) {
#ifdef _WIN32
    closesocket(fd);
#else
    close(fd);
#endif
}

static void sock_send_all(SockFd fd, std::string_view data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        auto n = ::send(fd, data.data() + sent, static_cast<int>(data.size() - sent), 0);
        assert(n > 0);
        sent += static_cast<std::size_t>(n);
    }
}

static std::string sock_recv_until(SockFd fd, std::string_view marker) {
    std::string buf;
    buf.reserve(1024);
    char tmp[256];
    while (buf.find(marker) == std::string::npos) {
        auto n = ::recv(fd, tmp, sizeof(tmp), 0);
        assert(n > 0);
        buf.append(tmp, static_cast<std::size_t>(n));
    }
    return buf;
}

static std::string sock_recv_n(SockFd fd, std::size_t n) {
    std::string buf(n, '\0');
    std::size_t got = 0;
    while (got < n) {
        auto r = ::recv(fd, buf.data() + got, static_cast<int>(n - got), 0);
        assert(r > 0);
        got += static_cast<std::size_t>(r);
    }
    return buf;
}

static SockFd connect_to(const char *host, uint16_t port) {
    SockFd fd = ::socket(AF_INET, SOCK_STREAM, 0);
    assert(fd != BAD_FD);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    ::inet_pton(AF_INET, host, &addr.sin_addr);

    int r = ::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
    assert(r == 0);
    return fd;
}

// ─── WebSocket handshake helper ──────────────────────────────────────────────

static void do_ws_handshake(SockFd fd, const char *host, uint16_t port, const char *path) {
    // Fixed key for deterministic test; server will compute Accept from it.
    const std::string key = "dGhlIHNhbXBsZSBub25jZQ==";
    const std::string expected_accept = usub::unet::ws::makeAcceptKey(key);

    std::string req =
        std::string("GET ") + path + " HTTP/1.1\r\n" +
        "Host: " + host + ":" + std::to_string(port) + "\r\n" +
        "Upgrade: websocket\r\n" +
        "Connection: Upgrade\r\n" +
        "Sec-WebSocket-Key: " + key + "\r\n" +
        "Sec-WebSocket-Version: 13\r\n" +
        "\r\n";

    sock_send_all(fd, req);

    const std::string resp = sock_recv_until(fd, "\r\n\r\n");
    assert(resp.find("101") != std::string::npos && "Expected 101 Switching Protocols");
    assert(resp.find(expected_accept) != std::string::npos && "Accept key mismatch");
}

// ─── Frame send/recv on raw socket ───────────────────────────────────────────

static void send_client_text(SockFd fd, std::string_view payload) {
    const std::array<uint8_t,4> key = {0x37, 0xfa, 0x21, 0x3d};
    const std::string wire = ClientFrameSerializer::text(payload, key);
    sock_send_all(fd, wire);
}

static void send_client_close(SockFd fd) {
    const std::array<uint8_t,4> key = {0x00, 0x00, 0x00, 0x00};
    const std::string wire = ClientFrameSerializer::close(CloseCode::NORMAL, key);
    sock_send_all(fd, wire);
}

// Read one complete server frame from the socket.
static ServerFrame recv_server_frame(SockFd fd) {
    // Read bytes and feed ServerFrameParser until complete.
    ServerFrameParser parser;
    ServerFrame frame;
    std::string buf;

    while (true) {
        char tmp[512];
        auto n = ::recv(fd, tmp, sizeof(tmp), 0);
        assert(n > 0);
        buf.append(tmp, static_cast<std::size_t>(n));

        std::string_view view{buf};
        auto begin = view.begin();
        auto result = parser.step(frame, begin, view.end());

        if (!result) {
            std::cerr << "ServerFrameParser error: " << result.error().message << "\n";
            assert(false);
        }
        if (*result) { break; }
    }
    return frame;
}

// ─── Tests ───────────────────────────────────────────────────────────────────

static constexpr uint16_t TEST_PORT = 29876;

static void test_echo_and_close() {
    sock_init();

    usub::Uvent uvent{1};

    // Build config programmatically: set the "HTTP.PlainTextStream" section
    // as a literal top-level key (getObject() supports this).
    usub::unet::core::Config cfg;
    usub::unet::core::Config::Object section;
    section["host"] = usub::unet::core::Config::Value{std::string("127.0.0.1")};
    section["port"] = usub::unet::core::Config::Value{static_cast<std::uint64_t>(TEST_PORT)};
    cfg.root["HTTP.PlainTextStream"] = usub::unet::core::Config::Value{std::move(section)};

    usub::unet::http::ServerImpl<
        usub::unet::http::router::Radix,
        usub::unet::core::stream::PlainText> server2{uvent, cfg};

    server2.handleUpgrade("GET", "/ws",
        upgradeHandler([](Connection conn) -> usub::uvent::task::Awaitable<void> {
            while (auto frame = co_await conn.recv()) {
                if (frame->opcode == static_cast<uint8_t>(Opcode::TEXT)) {
                    co_await conn.send_text(frame->payload);
                }
            }
        })
    );

    // Run uvent in a background thread.
    std::thread t([&]() { uvent.run(); });

    // Give the acceptor a moment to bind.
    std::this_thread::sleep_for(std::chrono::milliseconds{100});

    // ── Connect and upgrade ──
    SockFd fd = connect_to("127.0.0.1", TEST_PORT);
    do_ws_handshake(fd, "127.0.0.1", TEST_PORT, "/ws");

    std::cout << "  handshake OK\n";

    // ── Send a text frame, expect echo ──
    send_client_text(fd, "Hello WebSocket");
    {
        ServerFrame echo = recv_server_frame(fd);
        assert(echo.opcode == static_cast<uint8_t>(Opcode::TEXT));
        assert(echo.payload == "Hello WebSocket");
        std::cout << "  echo OK: \"" << echo.payload << "\"\n";
    }

    // ── Send another frame ──
    send_client_text(fd, "second message");
    {
        ServerFrame echo = recv_server_frame(fd);
        assert(echo.payload == "second message");
        std::cout << "  second echo OK\n";
    }

    // ── Send CLOSE, expect CLOSE back ──
    send_client_close(fd);
    {
        ServerFrame closing = recv_server_frame(fd);
        assert(closing.opcode == static_cast<uint8_t>(Opcode::CLOSE));
        std::cout << "  close handshake OK\n";
    }

    sock_close(fd);

    // Shut down uvent.
    uvent.stop();
    t.join();

    std::cout << "[PASS] test_echo_and_close\n";
}

// ─── main ────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "=== WebSocket server integration test ===\n\n";
    test_echo_and_close();
    std::cout << "\nAll tests passed.\n";
    return 0;
}
