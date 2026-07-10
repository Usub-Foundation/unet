#include "unet/http/v2/upgrade.hpp"

#include <array>
#include <cstdint>

#include "unet/http/router/radix.hpp"
#include "unet/http/router/regex.hpp"
#include "unet/http/v2/wire/frame_parser.hpp"


namespace usub::unet::http::v2 {

    namespace {
        // Lookup table: -1 invalid, 0..63 valid base64url alphabet positions.
        constexpr std::array<std::int8_t, 256> make_b64url_table() {
            std::array<std::int8_t, 256> t{};
            for (auto &v : t) v = -1;
            const char *alpha = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
            for (std::int8_t i = 0; i < 64; ++i)
                t[static_cast<std::uint8_t>(alpha[i])] = i;
            return t;
        }
        constexpr auto kB64UrlTable = make_b64url_table();
    }// namespace


    std::optional<std::vector<std::byte>> base64urlDecode(std::string_view s) {
        // Strip optional padding.
        while (!s.empty() && s.back() == '=') s.remove_suffix(1);

        std::vector<std::byte> out;
        out.reserve((s.size() * 3) / 4);

        std::uint32_t acc = 0;
        int           bits = 0;
        for (char c : s) {
            auto v = kB64UrlTable[static_cast<std::uint8_t>(c)];
            if (v < 0) return std::nullopt;
            acc = (acc << 6) | static_cast<std::uint32_t>(v);
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                out.push_back(static_cast<std::byte>((acc >> bits) & 0xff));
            }
        }
        return out;
    }


    std::optional<SettingsPayload> parseSettingsHeader(std::string_view header_value) {
        auto bytes = base64urlDecode(header_value);
        if (!bytes) return std::nullopt;

        FrameHeader fh{};
        fh.length    = static_cast<std::uint32_t>(bytes->size());
        fh.type      = std::to_underlying(FRAME_TYPE::SETTINGS);
        fh.stream_id = 0;
        auto p       = FrameParser::parseSettings(
                fh, std::span<const std::byte>{bytes->data(), bytes->size()});
        if (!p) return std::nullopt;
        return *p;
    }

    // Explicit instantiations to force template body checks at library build
    // time. Without these, the templates would only be checked if/when a
    // downstream call site instantiates them.
    template class ServerSession<usub::unet::http::router::Radix>;
    template class ServerSession<usub::unet::http::router::Regex>;

}// namespace usub::unet::http::v2
