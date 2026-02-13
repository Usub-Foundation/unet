#pragma once

#include <type_traits>
#include <utility>

#include <uvent/Uvent.h>

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
        usub::uvent::task::Awaitable<void> acceptLoop(OnConnection on_connection) {
            usub::uvent::net::TCPServerSocket server_socket{"0.0.0.0", 22813,
                                                            50,// backlog
                                                            usub::uvent::utils::net::IPV::IPV4,
                                                            usub::uvent::utils::net::TCP};
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
