#pragma once

// ServerFrameSerializer — serializes server→client frames; never masks.

#include <cstdint>
#include <string>
#include <string_view>

#include "unet/ws/core/frame.hpp"

namespace usub::unet::ws {

    class ServerFrameSerializer {
    public:
        [[nodiscard]] static std::string serialize(const ServerFrame &frame) {
            std::string out;
            const std::size_t psz = frame.payload.size();
            out.reserve(2u + 8u + psz);

            uint8_t b0 = frame.opcode & 0x0Fu;
            if (frame.fin)  { b0 |= 0x80u; }
            if (frame.rsv1) { b0 |= 0x40u; }
            if (frame.rsv2) { b0 |= 0x20u; }
            if (frame.rsv3) { b0 |= 0x10u; }
            out.push_back(static_cast<char>(b0));

            if (psz <= 125u) {
                out.push_back(static_cast<char>(psz));
            } else if (psz <= 0xFFFFu) {
                out.push_back(static_cast<char>(126u));
                out.push_back(static_cast<char>((psz >> 8u) & 0xFFu));
                out.push_back(static_cast<char>( psz        & 0xFFu));
            } else {
                out.push_back(static_cast<char>(127u));
                for (int i = 7; i >= 0; --i) {
                    out.push_back(static_cast<char>((psz >> (i * 8u)) & 0xFFu));
                }
            }

            out.append(frame.payload);
            return out;
        }

        [[nodiscard]] static std::string text(std::string_view payload, bool fin = true) {
            ServerFrame f;
            f.fin     = fin;
            f.opcode  = static_cast<uint8_t>(Opcode::TEXT);
            f.payload = std::string(payload);
            return serialize(f);
        }

        [[nodiscard]] static std::string binary(std::string_view payload, bool fin = true) {
            ServerFrame f;
            f.fin     = fin;
            f.opcode  = static_cast<uint8_t>(Opcode::BINARY);
            f.payload = std::string(payload);
            return serialize(f);
        }

        [[nodiscard]] static std::string ping(std::string_view payload = {}) {
            ServerFrame f;
            f.opcode  = static_cast<uint8_t>(Opcode::PING);
            f.payload = std::string(payload);
            return serialize(f);
        }

        [[nodiscard]] static std::string pong(std::string_view payload = {}) {
            ServerFrame f;
            f.opcode  = static_cast<uint8_t>(Opcode::PONG);
            f.payload = std::string(payload);
            return serialize(f);
        }

        [[nodiscard]] static std::string close(CloseCode code = CloseCode::NORMAL,
                                               std::string_view reason = {}) {
            ServerFrame f;
            f.opcode = static_cast<uint8_t>(Opcode::CLOSE);
            const auto c = static_cast<uint16_t>(code);
            f.payload.push_back(static_cast<char>((c >> 8u) & 0xFFu));
            f.payload.push_back(static_cast<char>( c        & 0xFFu));
            f.payload.append(reason);
            return serialize(f);
        }
    };

}  // namespace usub::unet::ws
