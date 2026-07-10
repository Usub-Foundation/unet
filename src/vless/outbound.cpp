#include "unet/vless/outbound.hpp"

#include <array>
#include <cstdio>
#include <string>
#include <utility>
#include <variant>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

#include <uvent/Uvent.h>

#include "unet/core/streams/plaintext.hpp"
#include "unet/core/transport/tcp.hpp"

namespace usub::unet::vless {

    namespace {
        using PlainTextStream = usub::unet::core::stream::PlainText;
        using ClientSocket = usub::uvent::net::TCPClientSocket;
        using OutboundTransport = usub::unet::core::transport::TCP<PlainTextStream, ClientSocket>;

        PlainTextStream &outboundStream() noexcept {
            static PlainTextStream s{};
            return s;
        }

        std::string formatIPv4(const std::array<std::byte, 4> &bytes) {
            char buf[INET_ADDRSTRLEN]{};
            std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
                          static_cast<unsigned>(std::to_integer<std::uint8_t>(bytes[0])),
                          static_cast<unsigned>(std::to_integer<std::uint8_t>(bytes[1])),
                          static_cast<unsigned>(std::to_integer<std::uint8_t>(bytes[2])),
                          static_cast<unsigned>(std::to_integer<std::uint8_t>(bytes[3])));
            return std::string(buf);
        }

        std::string formatIPv6(const std::array<std::byte, 16> &bytes) {
            char buf[INET6_ADDRSTRLEN]{};
            struct in6_addr a{};
            for (std::size_t i = 0; i < 16; ++i) a.s6_addr[i] = std::to_integer<std::uint8_t>(bytes[i]);
            if (!::inet_ntop(AF_INET6, &a, buf, sizeof(buf))) return {};
            return std::string(buf);
        }

        std::string hostFromAddress(const Address &address) {
            if (const auto *v4 = std::get_if<std::array<std::byte, 4>>(&address)) return formatIPv4(*v4);
            if (const auto *d = std::get_if<Domain>(&address)) return d->value;
            if (const auto *v6 = std::get_if<std::array<std::byte, 16>>(&address)) return formatIPv6(*v6);
            return {};
        }
    }// namespace

    usub::uvent::task::Awaitable<std::unique_ptr<usub::unet::core::transport::Transport>>
    dialTcp(const Address &address, std::uint16_t port) {
        std::string host = hostFromAddress(address);
        if (host.empty()) co_return nullptr;

        std::string port_str = std::to_string(port);
        ClientSocket sock;

        auto err = co_await sock.async_connect(host, port_str);
        if (err) co_return nullptr;

        co_return std::make_unique<OutboundTransport>(outboundStream(), std::move(sock));
    }

}// namespace usub::unet::vless
