#include "unet/ws/wire/server_frame_parser.hpp"

namespace usub::unet::ws {

    ServerFrameParser::ServerFrameParser(std::size_t maxPayload)
        : max_payload_(maxPayload) {}

    ServerFrameParser::ParserContext &ServerFrameParser::getContext() noexcept {
        return ctx_;
    }

    const ServerFrameParser::ParserContext &ServerFrameParser::getContext() const noexcept {
        return ctx_;
    }

    void ServerFrameParser::reset() noexcept {
        ctx_ = {};
    }

    std::expected<bool, FrameParseError> ServerFrameParser::step(
            ServerFrame &frame,
            std::string_view::const_iterator &begin,
            const std::string_view::const_iterator end) {
        while (begin != end) {
            const auto c = static_cast<uint8_t>(*begin);
            ++begin;

            switch (ctx_.state) {
            case STATE::BYTE0:
                frame = {};
                frame.fin = (c & 0x80u) != 0;
                frame.rsv1 = (c & 0x40u) != 0;
                frame.rsv2 = (c & 0x20u) != 0;
                frame.rsv3 = (c & 0x10u) != 0;
                frame.opcode = c & 0x0Fu;

                if (frame.rsv1 || frame.rsv2 || frame.rsv3) {
                    ctx_.state = STATE::FAILED;
                    return std::unexpected(FrameParseError{
                            .code = FrameParseError::CODE::RESERVED_BITS_SET,
                            .message = "RSV bits set without negotiated extension"});
                }
                ctx_.state = STATE::BYTE1;
                break;

            case STATE::BYTE1: {
                const bool masked = (c & 0x80u) != 0;
                if (masked) {
                    ctx_.state = STATE::FAILED;
                    return std::unexpected(FrameParseError{
                            .code = FrameParseError::CODE::MASKED_SERVER_FRAME,
                            .message = "server frame must not be masked (RFC 6455 section 5.1)"});
                }

                ctx_.payload_len = c & 0x7Fu;
                const bool control = (frame.opcode & 0x8u) != 0;

                if (control) {
                    if (!frame.fin) {
                        ctx_.state = STATE::FAILED;
                        return std::unexpected(FrameParseError{
                                .code = FrameParseError::CODE::CONTROL_FRAME_FRAGMENTED,
                                .message = "control frame must not be fragmented"});
                    }
                    if (ctx_.payload_len > 125u) {
                        ctx_.state = STATE::FAILED;
                        return std::unexpected(FrameParseError{
                                .code = FrameParseError::CODE::CONTROL_FRAME_TOO_LARGE,
                                .message = "control frame payload exceeds 125 bytes"});
                    }
                }

                if (ctx_.payload_len == 126u) {
                    ctx_.ext_len_bytes = 2;
                    ctx_.ext_len_idx = 0;
                    ctx_.payload_len = 0;
                    ctx_.state = STATE::EXT_LEN;
                } else if (ctx_.payload_len == 127u) {
                    ctx_.ext_len_bytes = 8;
                    ctx_.ext_len_idx = 0;
                    ctx_.payload_len = 0;
                    ctx_.state = STATE::EXT_LEN;
                } else if (ctx_.payload_len > 0u) {
                    frame.payload.clear();
                    frame.payload.reserve(static_cast<std::size_t>(ctx_.payload_len));
                    ctx_.state = STATE::PAYLOAD;
                } else {
                    ctx_.state = STATE::BYTE0;
                    return true;
                }
                break;
            }

            case STATE::EXT_LEN:
                ctx_.ext_len_buf[ctx_.ext_len_idx++] = c;
                if (ctx_.ext_len_idx == ctx_.ext_len_bytes) {
                    ctx_.payload_len = 0;
                    for (int i = 0; i < ctx_.ext_len_bytes; ++i) {
                        ctx_.payload_len = (ctx_.payload_len << 8u) | ctx_.ext_len_buf[i];
                    }
                    if (ctx_.payload_len > max_payload_) {
                        ctx_.state = STATE::FAILED;
                        return std::unexpected(FrameParseError{
                                .code = FrameParseError::CODE::PAYLOAD_TOO_LARGE,
                                .message = "payload exceeds maximum allowed size"});
                    }
                    if (ctx_.payload_len > 0u) {
                        frame.payload.clear();
                        frame.payload.reserve(static_cast<std::size_t>(ctx_.payload_len));
                        ctx_.state = STATE::PAYLOAD;
                    } else {
                        ctx_.state = STATE::BYTE0;
                        return true;
                    }
                }
                break;

            case STATE::PAYLOAD:
                frame.payload.push_back(static_cast<char>(c));
                if (frame.payload.size() == static_cast<std::size_t>(ctx_.payload_len)) {
                    ctx_.state = STATE::BYTE0;
                    return true;
                }
                break;

            case STATE::FAILED:
                return std::unexpected(FrameParseError{
                        .code = FrameParseError::CODE::FAILED_STATE,
                        .message = "parser is in failed state; call reset() to recover"});
            }
        }

        return false;
    }

}  // namespace usub::unet::ws
