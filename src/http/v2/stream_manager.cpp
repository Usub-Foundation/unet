#include "unet/http/v2/stream_manager.hpp"

namespace usub::unet::http::v2 {
    std::expected<std::string /*ack_frame*/, ERROR_CODE> StreamManager::handleInitialSettings(FrameHeader &frame_header,
                                                                                              std::string_view data) {
        static const std::uint8_t ACK = static_cast<std::uint8_t>(FLAGS::ACK);
        if (frame_header.type != FRAME_TYPE::SETTINGS || frame_header.stream_identifier != 0 ||
            (frame_header.flags & ACK) != 0) {
            return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
        }
        if ((frame_header.length % 6) != 0) { return std::unexpected(ERROR_CODE::FRAME_SIZE_ERROR); }
        return std::string{
                '\0',   '\0', '\0',     // length = 0
                '\x04',                 // type = SETTINGS
                '\x01',                 // flags = ACK
                '\0',   '\0', '\0', '\0'// stream id = 0
        };
    }

    v2::GenericFrame StreamManager::sendSettings() { return this->control_stream_.sendSettings(); }
}// namespace usub::unet::http::v2
