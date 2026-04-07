#pragma once

#include <functional>
#include <string_view>

#include <uvent/Uvent.h>

namespace usub::unet::core::stream {

    struct AsyncTransport {
        usub::uvent::task::Awaitable<std::string> read();
        usub::uvent::task::Awaitable<ssize_t> send(std::string_view data);
        usub::uvent::task::Awaitable<ssize_t> sendFile();
        usub::uvent::task::Awaitable<void> close();
    };

    struct Transport {
        // std::string read();
        // ssize_t send(std::string_view data);
        // ssize_t sendFile();
        // void close();
        std::function<usub::uvent::task::Awaitable<std::string>()> read;
        std::function<usub::uvent::task::Awaitable<ssize_t>(std::string_view)> send;
        std::function<usub::uvent::task::Awaitable<ssize_t>()> sendFile;
        std::function<usub::uvent::task::Awaitable<void>()> close;
    };
}// namespace usub::unet::core::stream
