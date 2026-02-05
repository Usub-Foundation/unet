#pragma once

#include <functional>
#include <string_view>

#include <uvent/Uvent.h>

namespace usub::unet::core::stream {

    struct Transport {
        void send();
        void sendFile();
        void close();
    };
}// namespace usub::unet::core::stream
