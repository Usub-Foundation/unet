#pragma once

#include <uvent/Uvent.h>

#include "unet/core/streams/stream.hpp"

namespace usub::unet::core::stream {

    class PlainText {
    public:
        PlainText() = default;
        ~PlainText() = default;

        template<typename Dispatcher>
        // requires requires {
        //     { &Dispatcher::on_read } -> std::same_as<usub::uvent::task::Awaitable<void> (Dispatcher::*)(std::string_view)>;
        //     { &Dispatcher::on_close } -> std::same_as<usub::uvent::task::Awaitable<void> (Dispatcher::*)()>;
        //     { &Dispatcher::on_error } -> std::same_as<usub::uvent::task::Awaitable<void> (Dispatcher::*)(std::error_code)>;
        // }
        static usub::uvent::task::Awaitable<void> readLoop(usub::uvent::net::TCPClientSocket socket,
                                                           Dispatcher dispatcher) {
            usub::uvent::utils::DynamicBuffer buffer;
            static constexpr size_t MAX_READ_SIZE = 16 * 1024;
            buffer.reserve(MAX_READ_SIZE);

            Transport transport{.send = [&](std::string_view out) -> usub::uvent::task::Awaitable<ssize_t> {
                co_return co_await socket.async_write((uint8_t *) out.data(), out.size());
            }};

            while (true) {
                buffer.clear();

                ssize_t rdsz = co_await socket.async_read(buffer, MAX_READ_SIZE);

                if (rdsz <= 0) {
                    co_await dispatcher.on_close();
                    break;
                }
                socket.set_timeout_ms(20000);
                co_await dispatcher.on_read(
                        std::string_view{reinterpret_cast<const char *>(buffer.data()), buffer.size()}, transport);
                // we need to pass socket for writing response, and for possible timeout reset for keep-alive, for example
                // or to close in case of error
            }
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

}// namespace usub::unet::core::stream