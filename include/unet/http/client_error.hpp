#pragma once

#include <cstdint>

namespace usub::unet::http {

    enum class ClientError : std::uint8_t {
        InvalidRequest,
        ConnectFailed,
        WriteFailed,
        ReadFailed,
        ParseFailed,
        Timeout,
        ProtocolError,
        NoStreamForScheme,
    };

}// namespace usub::unet::http
