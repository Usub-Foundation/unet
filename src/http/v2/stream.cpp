#include "unet/http/v2/stream.hpp"

inline void append_u16_be(std::vector<std::byte> &out, std::uint16_t v) {
    out.push_back(std::byte((v >> 8) & 0xFF));
    out.push_back(std::byte(v & 0xFF));
}

inline void append_u32_be(std::vector<std::byte> &out, std::uint32_t v) {
    out.push_back(std::byte((v >> 24) & 0xFF));
    out.push_back(std::byte((v >> 16) & 0xFF));
    out.push_back(std::byte((v >> 8) & 0xFF));
    out.push_back(std::byte(v & 0xFF));
}

namespace usub::unet::http::v2 {

    std::expected<std::string /*ack_frame*/, ERROR_CODE> ControlStream::handleSettings(FrameHeader &frame_header,
                                                                                       std::string_view data) {
        if (frame_header.flags & static_cast<std::uint8_t>(FLAGS::ACK)) {
            // ACK frame, no payload
            if (frame_header.length != 0) { return std::unexpected(ERROR_CODE::FRAME_SIZE_ERROR); }
            if (this->pending_settings_acks.empty()) { return std::unexpected(ERROR_CODE::PROTOCOL_ERROR); }
            auto &ack = this->pending_settings_acks.front();
            this->pending_settings_acks.pop();
            for (const auto &setting: ack.payload.settings) {
                switch (setting.identifier) {
                    case SETTINGS::HEADER_TABLE_SIZE:
                        this->server.header_table_size = setting.value;
                        break;
                    case SETTINGS::ENABLE_PUSH:
                        this->server.enable_push = setting.value;
                        break;
                    case SETTINGS::MAX_CONCURRENT_STREAMS:
                        this->server.max_concurrent_streams = setting.value;
                        break;
                    case SETTINGS::INITIAL_WINDOW_SIZE:
                        this->server.initial_window_size = setting.value;
                        break;
                    case SETTINGS::MAX_FRAME_SIZE:
                        this->server.max_frame_size = setting.value;
                        break;
                    case SETTINGS::MAX_HEADER_LIST_SIZE:
                        this->server.max_header_list_size = setting.value;
                        break;
                }
            }
            return std::string{};
        }
    }

    v2::GenericFrame ControlStream::sendSettings() {
        // RFC defaults
        constexpr std::uint32_t D_HEADER_TABLE_SIZE = 4096;
        constexpr std::uint32_t D_ENABLE_PUSH = 1;
        constexpr std::uint32_t D_INITIAL_WINDOW_SIZE = 65535;
        constexpr std::uint32_t D_MAX_FRAME_SIZE = 16384;
        // max_concurrent_streams: unset by default
        // max_header_list_size:   unset by default

        // Validity checks per RFC constraints
        if (server.enable_push != 0 && server.enable_push != 1) {
            throw std::runtime_error("SETTINGS_ENABLE_PUSH must be 0 or 1");
        }
        if (server.initial_window_size > 0x7FFFFFFFu) {// 2^31 - 1
            throw std::runtime_error("SETTINGS_INITIAL_WINDOW_SIZE out of range");
        }
        if (server.max_frame_size < 16384u || server.max_frame_size > 16777215u) {
            throw std::runtime_error("SETTINGS_MAX_FRAME_SIZE out of range");
        }

        SettingsPayload pl;
        pl.settings.reserve(6);

        // Only send non-defaults
        if (server.header_table_size != D_HEADER_TABLE_SIZE) {
            pl.settings.push_back({SETTINGS::HEADER_TABLE_SIZE, server.header_table_size});
        }
        if (server.enable_push != D_ENABLE_PUSH) { pl.settings.push_back({SETTINGS::ENABLE_PUSH, server.enable_push}); }
        if (server.max_concurrent_streams.has_value()) {
            pl.settings.push_back({SETTINGS::MAX_CONCURRENT_STREAMS, *server.max_concurrent_streams});
        }
        if (server.initial_window_size != D_INITIAL_WINDOW_SIZE) {
            pl.settings.push_back({SETTINGS::INITIAL_WINDOW_SIZE, server.initial_window_size});
        }
        if (server.max_frame_size != D_MAX_FRAME_SIZE) {
            pl.settings.push_back({SETTINGS::MAX_FRAME_SIZE, server.max_frame_size});
        }
        if (server.max_header_list_size.has_value()) {
            pl.settings.push_back({SETTINGS::MAX_HEADER_LIST_SIZE, *server.max_header_list_size});
        }

        std::vector<std::byte> bytes;
        bytes.reserve(pl.settings.size() * 6);

        for (const auto &s: pl.settings) {
            append_u16_be(bytes, static_cast<std::uint16_t>(s.identifier));
            append_u32_be(bytes, s.value);
        }

        v2::GenericFrame f;
        f.frame_header.type = FRAME_TYPE::SETTINGS;
        f.frame_header.flags = 0;            // not ACK
        f.frame_header.stream_identifier = 0;// stream 0
        f.payload = std::move(bytes);
        f.frame_header.length = static_cast<std::uint32_t>(f.payload.size());// 24-bit value on wire

        // Only track pending ack if we actually sent settings parameters.
        // (If payload is empty, it's still a valid SETTINGS frame, but ACK tracking is pointless.)
        if (!pl.settings.empty()) {
            pending_settings_acks.push(PendingSettingsAck{
                    .payload = std::move(pl),
                    .sent_at = std::chrono::steady_clock::now(),
            });
        }

        return f;
    }
}// namespace usub::unet::http::v2