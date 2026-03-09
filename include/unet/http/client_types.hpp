#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

#include "unet/http/error.hpp"

// TODO: Unify with error

namespace usub::unet::http {
    struct ClientError {
        enum class CODE : std::uint8_t {
            INVALID_REQUEST,
            CONNECT_FAILED,
            PROXY_FAILED,
            WRITE_FAILED,
            READ_FAILED,
            PARSE_FAILED,
            CLOSE_FAILED,
        };

        CODE code{CODE::INVALID_REQUEST};
        std::string message{};
        std::optional<ParseError> parse_error{};
    };

    struct ClientProxyOptions {
        std::string host{};
        std::uint16_t port{8080};
        std::optional<std::string> username{};
        std::optional<std::string> password{};
    };

    struct ClientRequestOptions {
        std::chrono::milliseconds connect_timeout{std::chrono::milliseconds{3000}};
        std::optional<ClientProxyOptions> proxy{};
    };
}// namespace usub::unet::http
