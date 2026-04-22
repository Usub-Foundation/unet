#include "unet/ws/wire/client_frame_serializer.hpp"

namespace usub::unet::ws {

    std::string ClientFrameSerializer::serialize(const ClientFrame &frame) {
        std::string output;
        const std::size_t payloadSize = frame.payload.size();
        output.reserve(2u + 8u + 4u + payloadSize);

        uint8_t byte0 = frame.opcode & 0x0Fu;
        if (frame.fin) {
            byte0 |= 0x80u;
        }
        if (frame.rsv1) {
            byte0 |= 0x40u;
        }
        if (frame.rsv2) {
            byte0 |= 0x20u;
        }
        if (frame.rsv3) {
            byte0 |= 0x10u;
        }
        output.push_back(static_cast<char>(byte0));

        if (payloadSize <= 125u) {
            output.push_back(static_cast<char>(0x80u | static_cast<uint8_t>(payloadSize)));
        } else if (payloadSize <= 0xFFFFu) {
            output.push_back(static_cast<char>(0x80u | 126u));
            output.push_back(static_cast<char>((payloadSize >> 8u) & 0xFFu));
            output.push_back(static_cast<char>(payloadSize & 0xFFu));
        } else {
            output.push_back(static_cast<char>(0x80u | 127u));
            for (int i = 7; i >= 0; --i) {
                output.push_back(static_cast<char>((payloadSize >> (i * 8u)) & 0xFFu));
            }
        }

        for (uint8_t byte: frame.mask_key) {
            output.push_back(static_cast<char>(byte));
        }

        for (std::size_t i = 0; i < payloadSize; ++i) {
            output.push_back(static_cast<char>(
                    static_cast<uint8_t>(frame.payload[i]) ^ frame.mask_key[i % 4u]));
        }

        return output;
    }

    std::string ClientFrameSerializer::text(std::string_view payload,
                                            std::array<uint8_t, 4> mask_key,
                                            bool fin) {
        ClientFrame frame;
        frame.fin = fin;
        frame.opcode = static_cast<uint8_t>(Opcode::TEXT);
        frame.mask_key = mask_key;
        frame.payload = std::string(payload);
        return serialize(frame);
    }

    std::string ClientFrameSerializer::binary(std::string_view payload,
                                              std::array<uint8_t, 4> mask_key,
                                              bool fin) {
        ClientFrame frame;
        frame.fin = fin;
        frame.opcode = static_cast<uint8_t>(Opcode::BINARY);
        frame.mask_key = mask_key;
        frame.payload = std::string(payload);
        return serialize(frame);
    }

    std::string ClientFrameSerializer::ping(std::string_view payload,
                                            std::array<uint8_t, 4> mask_key) {
        ClientFrame frame;
        frame.opcode = static_cast<uint8_t>(Opcode::PING);
        frame.mask_key = mask_key;
        frame.payload = std::string(payload);
        return serialize(frame);
    }

    std::string ClientFrameSerializer::pong(std::string_view payload,
                                            std::array<uint8_t, 4> mask_key) {
        ClientFrame frame;
        frame.opcode = static_cast<uint8_t>(Opcode::PONG);
        frame.mask_key = mask_key;
        frame.payload = std::string(payload);
        return serialize(frame);
    }

    std::string ClientFrameSerializer::close(CloseCode code,
                                             std::array<uint8_t, 4> mask_key,
                                             std::string_view reason) {
        ClientFrame frame;
        frame.opcode = static_cast<uint8_t>(Opcode::CLOSE);
        frame.mask_key = mask_key;

        const auto closeCode = static_cast<uint16_t>(code);
        frame.payload.push_back(static_cast<char>((closeCode >> 8u) & 0xFFu));
        frame.payload.push_back(static_cast<char>(closeCode & 0xFFu));
        frame.payload.append(reason);
        return serialize(frame);
    }

}  // namespace usub::unet::ws
