#pragma once

#include <cstdint>
#include <string_view>
#include <utility>

#include "unet/core/transport/transport.hpp"

namespace usub::unet::core::transport {

    // stream_ is a per-thread instance owned by the acceptor; socket_ is per-connection.
    template<class Stream, class Socket>
    class TCP final : public Transport {
    public:
        TCP(Stream &stream, Socket socket) noexcept : stream_(stream), socket_(std::move(socket)) {}

        Awaitable<ssize_t> read(Buffer &dst) override { return stream_.read(socket_, dst); }

        Awaitable<ssize_t> send(std::string_view bytes) override {
            co_await stream_.send(socket_, bytes);
            co_return static_cast<ssize_t>(bytes.size());
        }

        Awaitable<bool> sendFile(int fd, std::uint64_t length, std::uint64_t offset) override {
            return stream_.sendFile(socket_, fd, offset, length);
        }

        Awaitable<void> shutdown() override { return stream_.shutdown(socket_); }

        void close() noexcept override {
            stream_.dropSession(socket_);
            socket_.remove();
        }

        void updateTimeout(std::chrono::milliseconds ms) noexcept override {
            if (ms.count() > 0) socket_.update_timeout(static_cast<std::uint64_t>(ms.count()));
        }

        // After this call *this is moved-from — caller MUST replace the transport pointer.
        Socket releaseSocket() noexcept { return std::move(socket_); }
        Stream &stream() noexcept { return stream_; }

    private:
        Stream &stream_;
        Socket socket_;
    };

}// namespace usub::unet::core::transport
