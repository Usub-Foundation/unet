#pragma once

#include <chrono>
#include <cstdint>
#include <queue>
#include <utility>

#include "unet/http/core/request.hpp"
#include "unet/http/core/response.hpp"
#include "unet/http/v2/error.hpp"
#include "unet/http/v2/frames.hpp"
#include "unet/http/v2/wire/request_parser.hpp"
#include "unet/http/v2/wire/request_serializer.hpp"
#include "unet/http/v2/wire/response_parser.hpp"
#include "unet/http/v2/wire/response_serializer.hpp"

namespace usub::unet::http::v2 {

    struct FlowControl {
        std::int32_t recv_window{65535};
        std::int32_t send_window{65535};
    };

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
        std::uint32_t id{};
        STATE state{STATE::IDLE};
        FlowControl flow_control{};
        Request request{};
        Response response{};
    };

    struct ClientStream : public Stream {
        RequestSerializer request_writer{};
        ResponseParser response_reader{};
    };

    struct ServerStream : public Stream {
        RequestParser request_reader{};
        ResponseSerializer response_writer{};
    };
}// namespace usub::unet::http::v2