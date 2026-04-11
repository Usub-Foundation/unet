#pragma once

// WebSocket opening handshake utilities (RFC 6455 §4).
// SHA-1 and Base64 are implemented inline to avoid extra dependencies.

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace usub::unet::ws {

    static constexpr std::string_view MAGIC = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    [[nodiscard]] inline std::array<uint8_t, 20> sha1(std::string_view data) {
        uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};

        auto rotl = [](uint32_t v, unsigned n) -> uint32_t {
            return (v << n) | (v >> (32u - n));
        };

        std::vector<uint8_t> msg(data.begin(), data.end());
        const uint64_t bit_len = static_cast<uint64_t>(data.size()) * 8u;
        msg.push_back(0x80u);
        while (msg.size() % 64 != 56) { msg.push_back(0x00u); }
        for (int i = 7; i >= 0; --i) {
            msg.push_back(static_cast<uint8_t>((bit_len >> (i * 8)) & 0xFFu));
        }

        for (std::size_t off = 0; off < msg.size(); off += 64) {
            uint32_t w[80]{};
            for (int i = 0; i < 16; ++i) {
                w[i] = (uint32_t(msg[off + i * 4])     << 24u) |
                       (uint32_t(msg[off + i * 4 + 1]) << 16u) |
                       (uint32_t(msg[off + i * 4 + 2]) <<  8u) |
                        uint32_t(msg[off + i * 4 + 3]);
            }
            for (int i = 16; i < 80; ++i) {
                w[i] = rotl(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1u);
            }

            uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
            for (int i = 0; i < 80; ++i) {
                uint32_t f{}, k{};
                if      (i < 20) { f = (b & c) | (~b & d);              k = 0x5A827999u; }
                else if (i < 40) { f = b ^ c ^ d;                       k = 0x6ED9EBA1u; }
                else if (i < 60) { f = (b & c) | (b & d) | (c & d);    k = 0x8F1BBCDCu; }
                else             { f = b ^ c ^ d;                       k = 0xCA62C1D6u; }

                uint32_t tmp = rotl(a, 5u) + f + e + k + w[i];
                e = d; d = c; c = rotl(b, 30u); b = a; a = tmp;
            }
            h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
        }

        std::array<uint8_t, 20> out{};
        for (int i = 0; i < 5; ++i) {
            out[i * 4]     = static_cast<uint8_t>((h[i] >> 24u) & 0xFFu);
            out[i * 4 + 1] = static_cast<uint8_t>((h[i] >> 16u) & 0xFFu);
            out[i * 4 + 2] = static_cast<uint8_t>((h[i] >>  8u) & 0xFFu);
            out[i * 4 + 3] = static_cast<uint8_t>( h[i]         & 0xFFu);
        }
        return out;
    }

    [[nodiscard]] inline std::string base64Encode(std::span<const uint8_t> data) {
        static constexpr std::string_view chars =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        out.reserve(((data.size() + 2u) / 3u) * 4u);
        for (std::size_t i = 0; i < data.size(); i += 3) {
            const uint32_t grp =
                (uint32_t(data[i]) << 16u) |
                (i + 1 < data.size() ? uint32_t(data[i + 1]) << 8u : 0u) |
                (i + 2 < data.size() ? uint32_t(data[i + 2])       : 0u);
            out.push_back(chars[(grp >> 18u) & 0x3Fu]);
            out.push_back(chars[(grp >> 12u) & 0x3Fu]);
            out.push_back(i + 1 < data.size() ? chars[(grp >> 6u) & 0x3Fu] : '=');
            out.push_back(i + 2 < data.size() ? chars[ grp        & 0x3Fu] : '=');
        }
        return out;
    }

    // Compute the Sec-WebSocket-Accept value from the client's Sec-WebSocket-Key.
    [[nodiscard]] inline std::string makeAcceptKey(std::string_view clientKey) {
        std::string combined;
        combined.reserve(clientKey.size() + MAGIC.size());
        combined.append(clientKey);
        combined.append(MAGIC);
        const auto digest = sha1(combined);
        return base64Encode(digest);
    }

    // Returns false if the request is not a valid WebSocket upgrade.
    [[nodiscard]] inline bool validateRequest(std::string_view upgradeHeader,
                                              std::string_view connectionHeader,
                                              std::string_view versionHeader) {
        if (upgradeHeader.size() != 9) { return false; }
        static constexpr std::string_view ws = "websocket";
        for (std::size_t i = 0; i < 9; ++i) {
            const char c  = upgradeHeader[i];
            const char lo = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
            if (lo != ws[i]) { return false; }
        }
        if (connectionHeader.find("Upgrade") == std::string_view::npos &&
            connectionHeader.find("upgrade") == std::string_view::npos) { return false; }
        if (versionHeader != "13") { return false; }
        return true;
    }

}  // namespace usub::unet::ws
