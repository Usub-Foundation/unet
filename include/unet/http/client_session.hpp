#pragma once

#include <optional>

// TODO: Better place for all that

#include "unet/http/client_types.hpp"
#include "unet/http/core/response.hpp"

namespace usub::unet::http {
    struct ClientSessionState {
        Response response{};
        bool read_until_close_mode{false};
        bool complete{false};
        bool saw_bytes{false};
        std::optional<ClientError> error{};
    };
}// namespace usub::unet::http
