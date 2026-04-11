#pragma once

// ClientFrameSerializer — serializes client→server frames; always masks.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "unet/ws/core/frame.hpp"

namespace usub::unet::ws {

    class ClientFrameSerializer {
    public:
        [[nodiscard]] static std::string serialize(const ClientFrame &frame) {
            std::string out;
            const std::size_t psz = frame.payload.size();
            out.reserve(2u + 8u + 4u + psz);

            uint8_t b0 = frame.opcode & 0x0Fu;
            if (frame.fin)  { b0 |= 0x80u; }
            if (frame.rsv1) { b0 |= 0x40u; }
            if (frame.rsv2) { b0 |= 0x20u; }
            if (frame.rsv3) { b0 |= 0x10u; }
            out.push_back(static_cast<char>(b0));

            // MASK bit always set for client frames.
            if (psz <= 125u) {
                out.push_back(static_cast<char>(0x80u | static_cast<uint8_t>(psz)));
            } else if (psz <= 0xFFFFu) {
                out.push_back(static_cast<char>(0x80u | 126u));
                out.push_back(static_cast<char>((psz >> 8u) & 0xFFu));
                out.push_back(static_cast<char>( psz        & 0xFFu));
            } else {
                out.push_back(static_cast<char>(0x80u | 127u));
                for (int i = 7; i >= 0; --i) {
                    out.push_back(static_cast<char>((psz >> (i * 8u)) & 0xFFu));
                }
            }

            for (uint8_t k : frame.mask_key) { out.push_back(static_cast<char>(k)); }

            for (std::size_t i = 0; i < psz; ++i) {
                out.push_back(static_cast<char>(
                    static_cast<uint8_t>(frame.payload[i]) ^ frame.mask_key[i % 4u]));
            }
            return out;
        }

        [[nodiscard]] static std::string text(std::string_view payload,
                                              std::array<uint8_t, 4> mask_key,
                                              bool fin = true) {
            ClientFrame f;
            f.fin      = fin;
            f.opcode   = static_cast<uint8_t>(Opcode::TEXT);
            f.mask_key = mask_key;
            f.payload  = std::string(payload);
            return serialize(f);
        }

        [[nodiscard]] static std::string binary(std::string_view payload,
                                                std::array<uint8_t, 4> mask_key,
                                                bool fin = true) {
            ClientFrame f;
            f.fin      = fin;
            f.opcode   = static_cast<uint8_t>(Opcode::BINARY);
            f.mask_key = mask_key;
            f.payload  = std::string(payload);
            return serialize(f);
        }

        [[nodiscard]] static std::string ping(std::string_view payload,
                                              std::array<uint8_t, 4> mask_key) {
            ClientFrame f;
            f.opcode   = static_cast<uint8_t>(Opcode::PING);
            f.mask_key = mask_key;
            f.payload  = std::string(payload);
            return serialize(f);
        }

        [[nodiscard]] static std::string pong(std::string_view payload,
                                              std::array<uint8_t, 4> mask_key) {
            ClientFrame f;
            f.opcode   = static_cast<uint8_t>(Opcode::PONG);
            f.mask_key = mask_key;
            f.payload  = std::string(payload);
            return serialize(f);
        }

        [[nodiscard]] static std::string close(CloseCode code,
                                               std::array<uint8_t, 4> mask_key,
                                               std::string_view reason = {}) {
            ClientFrame f;
            f.opcode   = static_cast<uint8_t>(Opcode::CLOSE);
            f.mask_key = mask_key;
            const auto c = static_cast<uint16_t>(code);
            f.payload.push_back(static_cast<char>((c >> 8u) & 0xFFu));
            f.payload.push_back(static_cast<char>( c        & 0xFFu));
            f.payload.append(reason);
            return serialize(f);
        }
    };

}  // namespace usub::unet::ws
