#pragma once

#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include <uvent/Uvent.h>

#include "unet/core/acceptor.hpp"
#include "unet/core/config.hpp"

namespace usub::unet::core {
    namespace stream {
        class PlainText {
        public:
            static constexpr bool ssl{false};

            PlainText() = default;
            ~PlainText() = default;

            usub::uvent::task::Awaitable<std::string> negotiatedAlpn(usub::uvent::net::TCPClientSocket &) {
                co_return std::string{};
            }

            usub::uvent::task::Awaitable<ssize_t> read(usub::uvent::net::TCPClientSocket &socket,
                                                       usub::uvent::utils::DynamicBuffer &buffer) {
                static constexpr size_t MAX_READ_SIZE = 16 * 1024;
                buffer.reserve(MAX_READ_SIZE);

                buffer.clear();

                co_return co_await socket.async_read(buffer, MAX_READ_SIZE);
            }

            usub::uvent::task::Awaitable<void> send(usub::uvent::net::TCPClientSocket &socket, std::string_view data) {
                auto wrsz = co_await socket.async_write((uint8_t *) data.data(), data.size());
            }

            // TODO: async_sendfile() zero-copy on Linux/BSD/Darwin.
            usub::uvent::task::Awaitable<bool> sendFile(usub::uvent::net::TCPClientSocket &socket, int fd,
                                                        std::uint64_t offset, std::uint64_t length) {
                static constexpr std::size_t CHUNK = 64 * 1024;
                std::vector<std::uint8_t> buf(CHUNK);

#ifdef _WIN32
                if (_lseeki64(fd, static_cast<long long>(offset), SEEK_SET) < 0) co_return false;
#else
                if (::lseek(fd, static_cast<off_t>(offset), SEEK_SET) < 0) co_return false;
#endif
                std::uint64_t remaining = length;
                while (remaining > 0) {
                    const std::size_t want = (remaining < CHUNK) ? static_cast<std::size_t>(remaining) : CHUNK;
#ifdef _WIN32
                    const int rd = _read(fd, buf.data(), static_cast<unsigned>(want));
#else
                    const ssize_t rd = ::read(fd, buf.data(), want);
#endif
                    if (rd <= 0) co_return false;
                    const ssize_t wr = co_await socket.async_write(buf.data(), static_cast<std::size_t>(rd));
                    if (wr != static_cast<ssize_t>(rd)) co_return false;
                    remaining -= static_cast<std::uint64_t>(rd);
                }
                co_return true;
            }

            usub::uvent::task::Awaitable<void> shutdown(usub::uvent::net::TCPClientSocket &socket) {
                socket.shutdown();
                co_return;
            }

            void dropSession(usub::uvent::net::TCPClientSocket &) noexcept {}

        private:
        };
    }// namespace stream

    template<>
    class Acceptor<stream::PlainText> {
    public:
        Acceptor() = default;
        ~Acceptor() = default;

        template<class OnConnection>
        usub::uvent::task::Awaitable<void> acceptLoop(OnConnection on_connection, Config &config,
                                                     std::string_view prefix) {
            const Config::Object  empty_section{};
            const Config::Object *prefix_obj  = config.getObject(prefix);
            const Config::Object *section_ptr = prefix_obj ? config.getObject(*prefix_obj, "PlainTextStream")
                                                           : nullptr;
            const Config::Object &section     = section_ptr ? *section_ptr : empty_section;

            std::string host = config.getString(section, "host", "127.0.0.1");
            const std::uint64_t raw_port = config.getUInt(section, "port", 22813);
            const std::uint16_t port =
                    (raw_port <= static_cast<std::uint64_t>(std::numeric_limits<std::uint16_t>::max()))
                            ? static_cast<std::uint16_t>(raw_port)
                            : static_cast<std::uint16_t>(22813);

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

            // Pre-arm socket timer at accept so updateTimeout uses update_timeout in place.
            const std::uint64_t base_timeout = config.getUInt(section, "base_timeout", 20000);

            usub::uvent::net::TCPServerSocket server_socket{host, static_cast<int>(port), backlog, ip_version,
                                                            socket_type};
            stream::PlainText stream;

            for (;;) {
                auto soc = co_await server_socket.async_accept();
                if (!soc) { continue; }

                soc.value().set_timeout_ms(base_timeout);
                usub::uvent::system::co_spawn(on_connection(stream, std::move(soc.value())));
            }
            co_return;
        }
    };
}// namespace usub::unet::core
