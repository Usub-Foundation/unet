#pragma once

#include <chrono>
#include <cstdint>
#include <string_view>

#include "unet/core/io_provider.hpp"

namespace usub::unet::core::transport {

    class Transport {
    public:
        virtual ~Transport() = default;

        virtual Awaitable<ssize_t> read(Buffer &dst) = 0;
        virtual Awaitable<ssize_t> send(std::string_view bytes) = 0;
        virtual Awaitable<bool> sendFile(int fd, std::uint64_t length, std::uint64_t offset) = 0;
        virtual Awaitable<void> shutdown() = 0;

        virtual void close() noexcept {}

        virtual void updateTimeout(std::chrono::milliseconds /*ms*/) noexcept {}
    };

}// namespace usub::unet::core::transport
