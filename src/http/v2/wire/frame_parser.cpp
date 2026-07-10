#include "unet/http/v2/wire/frame_parser.hpp"

#include "unet/http/v2/wire/flags.hpp"


namespace usub::unet::http::v2 {

    namespace {
        inline std::uint32_t readU24Be(const std::byte *p) noexcept {
            return (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(p[0])) << 16) |
                   (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(p[1])) << 8) |
                   (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(p[2])));
        }
        inline std::uint32_t readU32Be(const std::byte *p) noexcept {
            return (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(p[0])) << 24) |
                   (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(p[1])) << 16) |
                   (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(p[2])) << 8) |
                   (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(p[3])));
        }
        inline std::uint16_t readU16Be(const std::byte *p) noexcept {
            return static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(p[0])) << 8) |
                (static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(p[1]))));
        }
        inline std::uint64_t readU64Be(const std::byte *p) noexcept {
            std::uint64_t v = 0;
            for (int i = 0; i < 8; ++i) {
                v = (v << 8) | static_cast<std::uint8_t>(std::to_integer<std::uint8_t>(p[i]));
            }
            return v;
        }
    }// namespace


    std::expected<FrameHeader, ERROR_CODE>
    FrameParser::parseFrameHeader(std::span<const std::byte> buf) noexcept {
        if (buf.size() < frame_header_size) return std::unexpected(ERROR_CODE::FRAME_SIZE_ERROR);
        FrameHeader h{};
        h.length    = readU24Be(buf.data());
        h.type      = std::to_integer<std::uint8_t>(buf[3]);
        h.flags     = std::to_integer<std::uint8_t>(buf[4]);
        h.stream_id = readU32Be(buf.data() + 5) & 0x7fffffffu;
        return h;
    }

    std::expected<DataPayload, ERROR_CODE>
    FrameParser::parseData(const FrameHeader &h, std::span<const std::byte> payload) noexcept {
        if (h.stream_id == 0) return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
        if (payload.size() != h.length) return std::unexpected(ERROR_CODE::FRAME_SIZE_ERROR);

        DataPayload out{};
        std::size_t i = 0;
        if (h.flags & FLAGS::PADDED) {
            if (payload.empty()) return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
            out.pad_length = std::to_integer<std::uint8_t>(payload[0]);
            if (out.pad_length + 1u > payload.size()) return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
            i = 1;
        }
        const std::size_t data_end = payload.size() - out.pad_length;
        out.data.assign(payload.begin() + i, payload.begin() + data_end);
        out.padding.assign(payload.begin() + data_end, payload.end());
        return out;
    }

    std::expected<HeadersPayload, ERROR_CODE>
    FrameParser::parseHeaders(const FrameHeader &h, std::span<const std::byte> payload) noexcept {
        if (h.stream_id == 0) return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
        if (payload.size() != h.length) return std::unexpected(ERROR_CODE::FRAME_SIZE_ERROR);

        HeadersPayload out{};
        std::size_t i = 0;
        if (h.flags & FLAGS::PADDED) {
            if (payload.empty()) return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
            out.pad_length = std::to_integer<std::uint8_t>(payload[0]);
            ++i;
        }
        if (h.flags & FLAGS::PRIORITY) {
            if (payload.size() < i + 5) return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
            const std::uint32_t dep = readU32Be(payload.data() + i);
            out.has_priority      = true;
            out.exclusive         = (dep & 0x80000000u) != 0;
            out.stream_dependency = dep & 0x7fffffffu;
            out.weight            = std::to_integer<std::uint8_t>(payload[i + 4]);
            i += 5;
        }
        if (i + out.pad_length > payload.size()) return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
        const std::size_t frag_end = payload.size() - out.pad_length;
        out.header_block_fragment.assign(payload.begin() + i, payload.begin() + frag_end);
        out.padding.assign(payload.begin() + frag_end, payload.end());
        return out;
    }

    std::expected<PriorityPayload, ERROR_CODE>
    FrameParser::parsePriority(const FrameHeader &h, std::span<const std::byte> payload) noexcept {
        if (h.stream_id == 0) return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
        if (payload.size() != 5) return std::unexpected(ERROR_CODE::FRAME_SIZE_ERROR);
        const std::uint32_t dep = readU32Be(payload.data());
        PriorityPayload out{};
        out.exclusive         = (dep & 0x80000000u) != 0;
        out.stream_dependency = dep & 0x7fffffffu;
        out.weight            = std::to_integer<std::uint8_t>(payload[4]);
        return out;
    }

    std::expected<RstStreamPayload, ERROR_CODE>
    FrameParser::parseRstStream(const FrameHeader &h, std::span<const std::byte> payload) noexcept {
        if (h.stream_id == 0) return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
        if (payload.size() != 4) return std::unexpected(ERROR_CODE::FRAME_SIZE_ERROR);
        return RstStreamPayload{readU32Be(payload.data())};
    }

    std::expected<SettingsPayload, ERROR_CODE>
    FrameParser::parseSettings(const FrameHeader &h, std::span<const std::byte> payload) noexcept {
        if (h.stream_id != 0) return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
        if (h.flags & FLAGS::ACK) {
            if (!payload.empty()) return std::unexpected(ERROR_CODE::FRAME_SIZE_ERROR);
            return SettingsPayload{};
        }
        if (payload.size() % 6 != 0) return std::unexpected(ERROR_CODE::FRAME_SIZE_ERROR);

        SettingsPayload out{};
        out.settings.reserve(payload.size() / 6);
        for (std::size_t i = 0; i < payload.size(); i += 6) {
            Setting s{};
            s.id    = readU16Be(payload.data() + i);
            s.value = readU32Be(payload.data() + i + 2);
            out.settings.push_back(s);
        }
        return out;
    }

    std::expected<PingPayload, ERROR_CODE>
    FrameParser::parsePing(const FrameHeader &h, std::span<const std::byte> payload) noexcept {
        if (h.stream_id != 0) return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
        if (payload.size() != 8) return std::unexpected(ERROR_CODE::FRAME_SIZE_ERROR);
        return PingPayload{readU64Be(payload.data())};
    }

    std::expected<GoAwayPayload, ERROR_CODE>
    FrameParser::parseGoaway(const FrameHeader &h, std::span<const std::byte> payload) noexcept {
        if (h.stream_id != 0) return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
        if (payload.size() < 8) return std::unexpected(ERROR_CODE::FRAME_SIZE_ERROR);
        GoAwayPayload out{};
        out.last_stream_id = readU32Be(payload.data()) & 0x7fffffffu;
        out.error_code     = readU32Be(payload.data() + 4);
        out.additional_debug_data.assign(payload.begin() + 8, payload.end());
        return out;
    }

    std::expected<WindowUpdatePayload, ERROR_CODE>
    FrameParser::parseWindowUpdate(const FrameHeader &, std::span<const std::byte> payload) noexcept {
        if (payload.size() != 4) return std::unexpected(ERROR_CODE::FRAME_SIZE_ERROR);
        const std::uint32_t inc = readU32Be(payload.data()) & 0x7fffffffu;
        if (inc == 0) return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
        return WindowUpdatePayload{inc};
    }

    std::expected<ContinuationPayload, ERROR_CODE>
    FrameParser::parseContinuation(const FrameHeader &h, std::span<const std::byte> payload) noexcept {
        if (h.stream_id == 0) return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
        if (payload.size() != h.length) return std::unexpected(ERROR_CODE::FRAME_SIZE_ERROR);
        ContinuationPayload out{};
        out.header_block_fragment.assign(payload.begin(), payload.end());
        return out;
    }

}// namespace usub::unet::http::v2
