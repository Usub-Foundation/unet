#pragma once

#include <cstdint>

namespace usub::unet::http {
    enum class STEP : std::uint8_t {
        CONTINUE,
        HEADERS,
        BODY,
        COMPLETE,
    };

    struct ParseStep {
        STEP kind{STEP::CONTINUE};
        bool complete{false};
    };
}// namespace usub::unet::http
