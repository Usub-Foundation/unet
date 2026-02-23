#pragma once

#include <chrono>
#include <cstdint>
#include <queue>
#include <utility>

#include "unet/http/core/request.hpp"
#include "unet/http/core/response.hpp"
#include "unet/http/v2/error.hpp"
#include "unet/http/v2/frames.hpp"
#include "unet/http/v2/request_parser.hpp"
#include "unet/http/v2/response_serializer.hpp"

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
        // RequestParser request_reader{};
        // ResponseSerializer response_writer{};
    };

    struct ClientStream : public Stream {
        RequestSerializer request_writer{};
        ResponseParser response_reader{};
    };

    struct ServerStream : public Stream {
        RequestParser request_reader{};
        ResponseSerializer response_writer{};
    };

    struct Settings {
        std::uint32_t header_table_size{4096};// octets
        std::uint32_t enable_push{1};         // Any value other than 0 or 1 MUST be treated as PROTOCOL_ERROR.
        std::optional<std::uint32_t> max_concurrent_streams{};// unset default
        std::uint32_t initial_window_size{65535};             //octets
        std::uint32_t max_frame_size{16384};                  // octets
        //maximum allowed frame size (2^24-1 or 16,777,215 octets), inclusive. Values outside
        //this range MUST emit PROTOCOL_ERROR.
        std::optional<std::uint32_t> max_header_list_size{};// unset by default
    };

    struct ControlStream {// Stream 0, special case as its no state machine, and not really a stream

        struct PendingSettingsAck {
            SettingsPayload payload;
            std::chrono::steady_clock::time_point sent_at;
        };

        Settings local;
        Settings remote;
        FlowControl flow_control{};

        std::expected<std::string /*ack_frame*/, ERROR_CODE> handleSettings(FrameHeader &frame_header,
                                                                            std::string_view data);

        v2::GenericFrame sendSettings();

        std::queue<PendingSettingsAck> pending_settings_acks{};
    };
}// namespace usub::unet::http::v2