#include "unet/ws/wire/server_frame_serializer.hpp"

namespace usub::unet::ws {

    std::string ServerFrameSerializer::serialize(const ServerFrame &frame) {
        std::string output;
        const std::size_t payloadSize = frame.payload.size();
        output.reserve(2u + 8u + payloadSize);

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
            output.push_back(static_cast<char>(payloadSize));
        } else if (payloadSize <= 0xFFFFu) {
            output.push_back(static_cast<char>(126u));
            output.push_back(static_cast<char>((payloadSize >> 8u) & 0xFFu));
            output.push_back(static_cast<char>(payloadSize & 0xFFu));
        } else {
            output.push_back(static_cast<char>(127u));
            for (int i = 7; i >= 0; --i) {
                output.push_back(static_cast<char>((payloadSize >> (i * 8u)) & 0xFFu));
            }
        }

        output.append(frame.payload);
        return output;
    }

    std::string ServerFrameSerializer::text(std::string_view payload, bool fin) {
        ServerFrame frame;
        frame.fin = fin;
        frame.opcode = static_cast<uint8_t>(Opcode::TEXT);
        frame.payload = std::string(payload);
        return serialize(frame);
    }

    std::string ServerFrameSerializer::binary(std::string_view payload, bool fin) {
        ServerFrame frame;
        frame.fin = fin;
        frame.opcode = static_cast<uint8_t>(Opcode::BINARY);
        frame.payload = std::string(payload);
        return serialize(frame);
    }

    std::string ServerFrameSerializer::ping(std::string_view payload) {
        ServerFrame frame;
        frame.opcode = static_cast<uint8_t>(Opcode::PING);
        frame.payload = std::string(payload);
        return serialize(frame);
    }

    std::string ServerFrameSerializer::pong(std::string_view payload) {
        ServerFrame frame;
        frame.opcode = static_cast<uint8_t>(Opcode::PONG);
        frame.payload = std::string(payload);
        return serialize(frame);
    }

    std::string ServerFrameSerializer::close(CloseCode code, std::string_view reason) {
        ServerFrame frame;
        frame.opcode = static_cast<uint8_t>(Opcode::CLOSE);

        const auto closeCode = static_cast<uint16_t>(code);
        frame.payload.push_back(static_cast<char>((closeCode >> 8u) & 0xFFu));
        frame.payload.push_back(static_cast<char>(closeCode & 0xFFu));
        frame.payload.append(reason);
        return serialize(frame);
    }

}  // namespace usub::unet::ws
