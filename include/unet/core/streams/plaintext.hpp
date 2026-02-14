#pragma once

#include <limits>
#include <type_traits>
#include <utility>

#include <uvent/Uvent.h>

#include "unet/core/config.hpp"
#include "unet/core/streams/stream.hpp"

namespace usub::unet::core {
    namespace stream {
        class PlainText {
        public:
            PlainText() = default;
            ~PlainText() = default;

            // template<typename Dispatcher>
            // static usub::uvent::task::Awaitable<void> readLoop(usub::uvent::net::TCPClientSocket socket,
            //                                                    Dispatcher dispatcher) {
            //     usub::uvent::utils::DynamicBuffer buffer;
            //     static constexpr size_t MAX_READ_SIZE = 16 * 1024;
            //     buffer.reserve(MAX_READ_SIZE);

            //     Transport transport{.send = [&](std::string_view out) -> usub::uvent::task::Awaitable<ssize_t> {
            //         co_return co_await socket.async_write((uint8_t *) out.data(), out.size());
            //     }};

            //     while (true) {
            //         buffer.clear();

            //         ssize_t rdsz = co_await socket.async_read(buffer, MAX_READ_SIZE);

            //         if (rdsz <= 0) {
            //             co_await dispatcher.on_close();
            //             break;
            //         }
            //         socket.set_timeout_ms(20000);
            //         co_await dispatcher.on_read(
            //                 std::string_view{reinterpret_cast<const char *>(buffer.data()), buffer.size()}, transport);
            //         // we need to pass socket for writing response, and for possible timeout reset for keep-alive, for example
            //         // or to close in case of error
            //     }
            // }

            usub::uvent::task::Awaitable<ssize_t> read(usub::uvent::net::TCPClientSocket socket,
                                                       usub::uvent::utils::DynamicBuffer &buffer) {
                static constexpr size_t MAX_READ_SIZE = 16 * 1024;
                buffer.reserve(MAX_READ_SIZE);

                buffer.clear();

                co_return co_await socket.async_read(buffer, MAX_READ_SIZE);
            }

            usub::uvent::task::Awaitable<void> send(usub::uvent::net::TCPClientSocket socket, std::string_view data) {
                auto wrsz = co_await socket.async_write((uint8_t *) data.data(), data.size());
            }

            usub::uvent::task::Awaitable<void> sendFile(usub::uvent::net::TCPClientSocket socket) { co_return; }

            usub::uvent::task::Awaitable<void> shutdown(usub::uvent::net::TCPClientSocket socket) {
                socket.shutdown();
                co_return;
            }

        private:
        };
    }// namespace stream

    template<>
    class Acceptor<stream::PlainText> {
    public:
        Acceptor() = default;
        ~Acceptor() = default;

        template<class OnConnection>
        usub::uvent::task::Awaitable<void> acceptLoop(OnConnection on_connection, Config &config) {
            const Config::Object empty_section{};
            const Config::Object *section_ptr = config.getObject("HTTP.PlainTextStream");
            const Config::Object &section = section_ptr ? *section_ptr : empty_section;

            std::string host = config.getString(section, "host", "127.0.0.1");
            const std::uint64_t raw_port = config.getUInt(section, "port", 80);
            const std::uint16_t port =
                    (raw_port <= static_cast<std::uint64_t>(std::numeric_limits<std::uint16_t>::max()))
                            ? static_cast<std::uint16_t>(raw_port)
                            : static_cast<std::uint16_t>(80);

            std::int64_t backlog_cfg = config.getInt(section, "backlog", 50);
            if (backlog_cfg <= 0) { backlog_cfg = 50; }
            const int backlog = (backlog_cfg > static_cast<std::int64_t>(std::numeric_limits<int>::max()))
                                        ? std::numeric_limits<int>::max()
                                        : static_cast<int>(backlog_cfg);

            const std::int64_t version = config.getInt(section, "version", 4);
            const auto ip_version =
                    (version == 6) ? usub::uvent::utils::net::IPV::IPV6 : usub::uvent::utils::net::IPV::IPV4;

            std::string tcp = config.getString(section, "tcp", "tcp");
            for (char &ch: tcp) {
                if (ch >= 'A' && ch <= 'Z') { ch = static_cast<char>(ch - 'A' + 'a'); }
            }
            const auto socket_type = (tcp == "udp") ? usub::uvent::utils::net::UDP : usub::uvent::utils::net::TCP;

            usub::uvent::net::TCPServerSocket server_socket{host, static_cast<int>(port), backlog, ip_version,
                                                            socket_type};
            stream::PlainText stream;

            for (;;) {
                auto soc = co_await server_socket.async_accept();
                if (!soc) { continue; }

                usub::uvent::system::co_spawn(on_connection(stream, std::move(soc.value())));
            }
            co_return;
        }
    };
}// namespace usub::unet::core
