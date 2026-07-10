#include "unet/ws/wire/frame_serializer.hpp"

#include <array>
#include <cstdint>
#include <cstring>

namespace usub::unet::ws {

    namespace {
        void writeLength(std::string &out, bool mask_bit, std::uint64_t len) {
            std::uint8_t header_byte;
            if (len < 126u) {
                header_byte = static_cast<std::uint8_t>(len);
                if (mask_bit) header_byte |= 0x80u;
                out.push_back(static_cast<char>(header_byte));
            } else if (len <= 0xFFFFu) {
                header_byte = 126u;
                if (mask_bit) header_byte |= 0x80u;
                out.push_back(static_cast<char>(header_byte));
                out.push_back(static_cast<char>((len >> 8u) & 0xFFu));
                out.push_back(static_cast<char>( len        & 0xFFu));
            } else {
                header_byte = 127u;
                if (mask_bit) header_byte |= 0x80u;
                out.push_back(static_cast<char>(header_byte));
                for (int i = 7; i >= 0; --i) {
                    out.push_back(static_cast<char>((len >> (i * 8u)) & 0xFFu));
                }
            }
        }
    }// namespace

    std::string FrameSerializer::serialize(const Frame &frame) {
        std::string out;
        const std::size_t payload_size = frame.payload.size();
        out.reserve(2u + 8u + 4u + payload_size);

        // Byte 0.
        std::uint8_t byte0 = static_cast<std::uint8_t>(frame.opcode) & 0x0Fu;
        if (frame.fin)  byte0 |= 0x80u;
        if (frame.rsv1) byte0 |= 0x40u;
        if (frame.rsv2) byte0 |= 0x20u;
        if (frame.rsv3) byte0 |= 0x10u;
        out.push_back(static_cast<char>(byte0));

        // Byte 1 + extended length.
        writeLength(out, frame.masking_key.has_value(), payload_size);

        // Masking key + masked payload.
        if (frame.masking_key) {
            const auto &key = *frame.masking_key;
            out.push_back(static_cast<char>(key[0]));
            out.push_back(static_cast<char>(key[1]));
            out.push_back(static_cast<char>(key[2]));
            out.push_back(static_cast<char>(key[3]));
            // Bulk XOR using word-wide loads.
            std::uint32_t key_u32;
            std::memcpy(&key_u32, key.data(), 4);
            const std::size_t start = out.size();
            out.resize(start + payload_size);
            char *dst = out.data() + start;
            std::size_t i = 0;
            for (; i + 4 <= payload_size; i += 4) {
                std::uint32_t chunk;
                std::memcpy(&chunk, frame.payload.data() + i, 4);
                chunk ^= key_u32;
                std::memcpy(dst + i, &chunk, 4);
            }
            for (; i < payload_size; ++i) {
                dst[i] = static_cast<char>(
                        static_cast<std::uint8_t>(frame.payload[i]) ^ key[i % 4]);
            }
        } else {
            out.append(frame.payload);
        }
        return out;
    }

    std::string FrameSerializer::text(std::string_view payload, bool fin) {
        Frame f;
        f.fin     = fin;
        f.opcode  = OPCODE::TEXT;
        f.payload = std::string(payload);
        return serialize(f);
    }

    std::string FrameSerializer::binary(std::string_view payload, bool fin) {
        Frame f;
        f.fin     = fin;
        f.opcode  = OPCODE::BINARY;
        f.payload = std::string(payload);
        return serialize(f);
    }

    namespace {
        std::string makeData(OPCODE op, std::string_view payload, bool fin) {
            Frame f;
            f.fin     = fin;
            f.opcode  = op;
            f.payload = std::string(payload);
            return FrameSerializer::serialize(f);
        }
        std::string makeControl(OPCODE op, std::string_view payload) {
            Frame f;
            f.opcode  = op;
            f.payload = std::string(payload);
            return FrameSerializer::serialize(f);
        }
    }// namespace

    std::string FrameSerializer::reserved3(std::string_view p, bool fin) { return makeData(OPCODE::RESERVED_3, p, fin); }
    std::string FrameSerializer::reserved4(std::string_view p, bool fin) { return makeData(OPCODE::RESERVED_4, p, fin); }
    std::string FrameSerializer::reserved5(std::string_view p, bool fin) { return makeData(OPCODE::RESERVED_5, p, fin); }
    std::string FrameSerializer::reserved6(std::string_view p, bool fin) { return makeData(OPCODE::RESERVED_6, p, fin); }
    std::string FrameSerializer::reserved7(std::string_view p, bool fin) { return makeData(OPCODE::RESERVED_7, p, fin); }

    std::string FrameSerializer::ping(std::string_view payload) {
        Frame f;
        f.opcode  = OPCODE::PING;
        f.payload = std::string(payload);
        return serialize(f);
    }

    std::string FrameSerializer::pong(std::string_view payload) {
        Frame f;
        f.opcode  = OPCODE::PONG;
        f.payload = std::string(payload);
        return serialize(f);
    }

    std::string FrameSerializer::close(CLOSE_CODE code, std::string_view reason) {
        Frame f;
        f.opcode = OPCODE::CLOSE;
        const auto c = static_cast<std::uint16_t>(code);
        f.payload.push_back(static_cast<char>((c >> 8u) & 0xFFu));
        f.payload.push_back(static_cast<char>( c        & 0xFFu));
        f.payload.append(reason);
        return serialize(f);
    }

    std::string FrameSerializer::reservedB(std::string_view p) { return makeControl(OPCODE::RESERVED_B, p); }
    std::string FrameSerializer::reservedC(std::string_view p) { return makeControl(OPCODE::RESERVED_C, p); }
    std::string FrameSerializer::reservedD(std::string_view p) { return makeControl(OPCODE::RESERVED_D, p); }
    std::string FrameSerializer::reservedE(std::string_view p) { return makeControl(OPCODE::RESERVED_E, p); }
    std::string FrameSerializer::reservedF(std::string_view p) { return makeControl(OPCODE::RESERVED_F, p); }

}// namespace usub::unet::ws
