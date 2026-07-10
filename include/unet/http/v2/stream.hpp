#pragma once

#include <cstdint>
#include <memory>

#include "unet/http/core/request.hpp"
#include "unet/http/core/response.hpp"
#include "unet/http/v2/flow_control.hpp"
#include "unet/http/v2/stream_transport.hpp"
#include "unet/http/v2/wire/request_parser.hpp"
#include "unet/http/v2/wire/response_serializer.hpp"


namespace usub::unet::http::v2 {

    struct Stream {
        enum class STATE : std::uint8_t {
            IDLE,
            RESERVED_LOCAL,
            RESERVED_REMOTE,
            OPEN,
            HALF_CLOSED_LOCAL,
            HALF_CLOSED_REMOTE,
            CLOSED,
        };

        std::uint32_t  id{};
        STATE          state{STATE::IDLE};
        FlowControl    flow_control{};
        bool           tunnel{false};
        RequestReader  request{};
        ResponseWriter response{};

        RequestParser      request_parser{};
        ResponseSerializer response_serializer{};

        std::uint32_t continuation_count{0};
    };

}// namespace usub::unet::http::v2
