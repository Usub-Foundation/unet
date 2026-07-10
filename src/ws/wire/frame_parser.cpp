#include "unet/ws/wire/frame_parser.hpp"

namespace usub::unet::ws {

    std::expected<void, FrameParseError> FrameParser::stepHeader(FrameHeader &header,
                                                                 std::string_view::const_iterator &begin,
                                                                 const std::string_view::const_iterator end) {
        for (;;) {
            switch (context_.state) {
                case STATE::BYTE0: {
                    if (begin == end) return {};
                    const std::uint8_t c = static_cast<std::uint8_t>(*begin++);
                    header = {};
                    header.fin = (c & 0x80u) != 0;
                    header.rsv1 = (c & 0x40u) != 0;
                    header.rsv2 = (c & 0x20u) != 0;
                    header.rsv3 = (c & 0x10u) != 0;
                    header.opcode = static_cast<OPCODE>(c & 0x0Fu);

                    if (header.rsv1 || header.rsv2 || header.rsv3) {
                        context_.state = STATE::FAILED;
                        return std::unexpected(FrameParseError{.code = FrameParseError::CODE::RESERVED_BITS_SET,
                                                               .message = "RSV bits set without negotiated extension"});
                    }
                    context_.state = STATE::BYTE1;
                    [[fallthrough]];
                }
                case STATE::BYTE1: {
                    if (begin == end) return {};
                    const std::uint8_t c = static_cast<std::uint8_t>(*begin++);
                    header.mask = (c & 0x80u) != 0;
                    header.payload_length = c & 0x7Fu;

                    // RFC §5.4 / §5.5 — control frame constraints.
                    const std::uint8_t op = static_cast<std::uint8_t>(header.opcode);
                    if ((op & 0x8u) != 0) {
                        if (!header.fin) {
                            context_.state = STATE::FAILED;
                            return std::unexpected(
                                    FrameParseError{.code = FrameParseError::CODE::CONTROL_FRAME_FRAGMENTED,
                                                    .message = "control frame must not be fragmented"});
                        }
                        if (header.payload_length > 125u) {
                            context_.state = STATE::FAILED;
                            return std::unexpected(
                                    FrameParseError{.code = FrameParseError::CODE::CONTROL_FRAME_TOO_LARGE,
                                                    .message = "control frame payload exceeds 125 bytes"});
                        }
                    }

                    context_.bytes_in_field = 0;
                    if (header.payload_length == 126u) {
                        header.extended_payload_length = std::uint16_t{0};
                        context_.state = STATE::EXT_LEN_16;
                    } else if (header.payload_length == 127u) {
                        header.extended_payload_length = std::uint64_t{0};
                        context_.state = STATE::EXT_LEN_64;
                    } else {
                        header.extended_payload_length = std::monostate{};
                        context_.payload_len = header.payload_length;
                        if (header.mask) {
                            header.masking_key = std::array<std::uint8_t, 4>{};
                            context_.state = STATE::MASK_KEY;
                        } else {
                            context_.state = STATE::PAYLOAD;
                            return {};
                        }
                    }
                    break;
                }

                case STATE::EXT_LEN_16: {
                    while (context_.bytes_in_field < 2) {
                        if (begin == end) return {};
                        auto &v = std::get<std::uint16_t>(header.extended_payload_length);
                        v = static_cast<std::uint16_t>((v << 8u) | static_cast<std::uint8_t>(*begin++));
                        ++context_.bytes_in_field;
                    }
                    context_.payload_len = std::get<std::uint16_t>(header.extended_payload_length);
                    context_.bytes_in_field = 0;
                    if (header.mask) {
                        header.masking_key = std::array<std::uint8_t, 4>{};
                        context_.state = STATE::MASK_KEY;
                    } else {
                        context_.state = STATE::PAYLOAD;
                        return {};
                    }
                    break;
                }

                case STATE::EXT_LEN_64: {
                    while (context_.bytes_in_field < 8) {
                        if (begin == end) return {};
                        auto &v = std::get<std::uint64_t>(header.extended_payload_length);
                        v = (v << 8u) | static_cast<std::uint8_t>(*begin++);
                        ++context_.bytes_in_field;
                    }
                    context_.payload_len = std::get<std::uint64_t>(header.extended_payload_length);
                    context_.bytes_in_field = 0;
                    if (header.mask) {
                        header.masking_key = std::array<std::uint8_t, 4>{};
                        context_.state = STATE::MASK_KEY;
                    } else {
                        context_.state = STATE::PAYLOAD;
                        return {};
                    }
                    break;
                }

                case STATE::MASK_KEY: {
                    while (context_.bytes_in_field < 4) {
                        if (begin == end) return {};
                        (*header.masking_key)[context_.bytes_in_field++] = static_cast<std::uint8_t>(*begin++);
                    }
                    context_.bytes_in_field = 0;
                    context_.state = STATE::PAYLOAD;
                    return {};
                }

                case STATE::PAYLOAD:
                    // Header already complete on a previous call; idempotent.
                    return {};

                case STATE::FAILED:
                    return std::unexpected(
                            FrameParseError{.code = FrameParseError::CODE::FAILED_STATE,
                                            .message = "parser in FAILED state; call reset() to recover"});
            }
        }
    }

}// namespace usub::unet::ws
