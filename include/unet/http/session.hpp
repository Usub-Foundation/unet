#pragma once

#include <cstdint>

#include "unet/http/core/message.hpp"

namespace usub::unet::http {

    template<enum VERSION>
    class ClientSession;

    enum class RouteKind : uint8_t { Normal, Upgrade };

}// namespace usub::unet::http
