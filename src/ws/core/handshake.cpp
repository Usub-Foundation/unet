#include "unet/ws/core/handshake.hpp"

#include <vector>

namespace usub::unet::ws {

    std::array<uint8_t, 20> sha1(std::string_view data) {
        uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};

        const auto rotl = [](uint32_t value, unsigned shift) -> uint32_t {
            return (value << shift) | (value >> (32u - shift));
        };

        std::vector<uint8_t> message(data.begin(), data.end());
        const uint64_t bitLen = static_cast<uint64_t>(data.size()) * 8u;
        message.push_back(0x80u);
        while (message.size() % 64 != 56) {
            message.push_back(0x00u);
        }
        for (int i = 7; i >= 0; --i) {
            message.push_back(static_cast<uint8_t>((bitLen >> (i * 8)) & 0xFFu));
        }

        for (std::size_t offset = 0; offset < message.size(); offset += 64) {
            uint32_t w[80]{};
            for (int i = 0; i < 16; ++i) {
                w[i] = (uint32_t(message[offset + i * 4]) << 24u) |
                       (uint32_t(message[offset + i * 4 + 1]) << 16u) |
                       (uint32_t(message[offset + i * 4 + 2]) << 8u) |
                       uint32_t(message[offset + i * 4 + 3]);
            }
            for (int i = 16; i < 80; ++i) {
                w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1u);
            }

            uint32_t a = h[0];
            uint32_t b = h[1];
            uint32_t c = h[2];
            uint32_t d = h[3];
            uint32_t e = h[4];

            for (int i = 0; i < 80; ++i) {
                uint32_t f{};
                uint32_t k{};
                if (i < 20) {
                    f = (b & c) | (~b & d);
                    k = 0x5A827999u;
                } else if (i < 40) {
                    f = b ^ c ^ d;
                    k = 0x6ED9EBA1u;
                } else if (i < 60) {
                    f = (b & c) | (b & d) | (c & d);
                    k = 0x8F1BBCDCu;
                } else {
                    f = b ^ c ^ d;
                    k = 0xCA62C1D6u;
                }

                const uint32_t next = rotl(a, 5u) + f + e + k + w[i];
                e = d;
                d = c;
                c = rotl(b, 30u);
                b = a;
                a = next;
            }

            h[0] += a;
            h[1] += b;
            h[2] += c;
            h[3] += d;
            h[4] += e;
        }

        std::array<uint8_t, 20> output{};
        for (int i = 0; i < 5; ++i) {
            output[i * 4] = static_cast<uint8_t>((h[i] >> 24u) & 0xFFu);
            output[i * 4 + 1] = static_cast<uint8_t>((h[i] >> 16u) & 0xFFu);
            output[i * 4 + 2] = static_cast<uint8_t>((h[i] >> 8u) & 0xFFu);
            output[i * 4 + 3] = static_cast<uint8_t>(h[i] & 0xFFu);
        }
        return output;
    }

    std::string base64Encode(std::span<const uint8_t> data) {
        static constexpr std::string_view chars =
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        std::string output;
        output.reserve(((data.size() + 2u) / 3u) * 4u);

        for (std::size_t i = 0; i < data.size(); i += 3) {
            const uint32_t group = (uint32_t(data[i]) << 16u) |
                                   (i + 1 < data.size() ? uint32_t(data[i + 1]) << 8u : 0u) |
                                   (i + 2 < data.size() ? uint32_t(data[i + 2]) : 0u);

            output.push_back(chars[(group >> 18u) & 0x3Fu]);
            output.push_back(chars[(group >> 12u) & 0x3Fu]);
            output.push_back(i + 1 < data.size() ? chars[(group >> 6u) & 0x3Fu] : '=');
            output.push_back(i + 2 < data.size() ? chars[group & 0x3Fu] : '=');
        }

        return output;
    }

    std::string makeAcceptKey(std::string_view clientKey) {
        std::string combined;
        combined.reserve(clientKey.size() + MAGIC.size());
        combined.append(clientKey);
        combined.append(MAGIC);
        return base64Encode(sha1(combined));
    }

    bool validateRequest(std::string_view upgradeHeader,
                         std::string_view connectionHeader,
                         std::string_view versionHeader) {
        if (upgradeHeader.size() != 9) {
            return false;
        }

        static constexpr std::string_view websocket = "websocket";
        for (std::size_t i = 0; i < websocket.size(); ++i) {
            const char c = upgradeHeader[i];
            const char lower = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
            if (lower != websocket[i]) {
                return false;
            }
        }

        if (connectionHeader.find("Upgrade") == std::string_view::npos &&
            connectionHeader.find("upgrade") == std::string_view::npos) {
            return false;
        }

        return versionHeader == "13";
    }

}  // namespace usub::unet::ws
