#include "unet/http/v2/wire/frame_serializer.hpp"

#include <cstring>
#include <utility>
#include <vector>

#include "unet/http/v2/wire/flags.hpp"


namespace usub::unet::http::v2 {

    namespace {
        inline void writeU24Be(std::uint32_t v, std::byte *p) noexcept {
            p[0] = static_cast<std::byte>((v >> 16) & 0xff);
            p[1] = static_cast<std::byte>((v >> 8) & 0xff);
            p[2] = static_cast<std::byte>(v & 0xff);
        }
        inline void writeU32Be(std::uint32_t v, std::byte *p) noexcept {
            p[0] = static_cast<std::byte>((v >> 24) & 0xff);
            p[1] = static_cast<std::byte>((v >> 16) & 0xff);
            p[2] = static_cast<std::byte>((v >> 8) & 0xff);
            p[3] = static_cast<std::byte>(v & 0xff);
        }
        inline void writeU16Be(std::uint16_t v, std::byte *p) noexcept {
            p[0] = static_cast<std::byte>((v >> 8) & 0xff);
            p[1] = static_cast<std::byte>(v & 0xff);
        }
        inline void writeU64Be(std::uint64_t v, std::byte *p) noexcept {
            for (int i = 7; i >= 0; --i) {
                p[i] = static_cast<std::byte>(v & 0xff);
                v >>= 8;
            }
        }

        inline std::string buildFrame(std::uint8_t type, std::uint8_t flags,
                                       std::uint32_t stream_id,
                                       std::span<const std::byte> payload) {
            FrameHeader h{};
            h.length    = static_cast<std::uint32_t>(payload.size());
            h.type      = type;
            h.flags     = flags;
            h.stream_id = stream_id;
            return FrameSerializer::serializeFrame(h, payload);
        }
    }// namespace


    void FrameSerializer::writeFrameHeader(const FrameHeader &header, std::span<std::byte> out) noexcept {
        writeU24Be(header.length & 0x00ffffffu, out.data());
        out[3] = static_cast<std::byte>(header.type);
        out[4] = static_cast<std::byte>(header.flags);
        writeU32Be(header.stream_id & 0x7fffffffu, out.data() + 5);
    }

    std::string FrameSerializer::writeFrameHeaderString(const FrameHeader &header) {
        std::string s(frame_header_size, '\0');
        std::byte buf[frame_header_size];
        writeFrameHeader(header, std::span<std::byte>{buf, frame_header_size});
        std::memcpy(s.data(), buf, frame_header_size);
        return s;
    }

    std::string FrameSerializer::serializeFrame(const FrameHeader &header,
                                                  std::span<const std::byte> payload) {
        std::string out;
        out.resize(frame_header_size + payload.size());
        std::byte hdr[frame_header_size];
        writeFrameHeader(header, std::span<std::byte>{hdr, frame_header_size});
        std::memcpy(out.data(), hdr, frame_header_size);
        if (!payload.empty()) {
            std::memcpy(out.data() + frame_header_size, payload.data(), payload.size());
        }
        return out;
    }

    std::string FrameSerializer::serializeSettings(const SettingsPayload &p, std::uint8_t flags) {
        std::vector<std::byte> buf(p.settings.size() * 6);
        for (std::size_t i = 0; i < p.settings.size(); ++i) {
            writeU16Be(p.settings[i].id,    buf.data() + i * 6);
            writeU32Be(p.settings[i].value, buf.data() + i * 6 + 2);
        }
        return buildFrame(std::to_underlying(FRAME_TYPE::SETTINGS), flags, 0, buf);
    }

    std::string FrameSerializer::serializeSettingsAck() {
        return buildFrame(std::to_underlying(FRAME_TYPE::SETTINGS),
                           static_cast<std::uint8_t>(FLAGS::ACK), 0, {});
    }

    std::string FrameSerializer::serializeWindowUpdate(std::uint32_t stream_id, std::uint32_t increment) {
        std::byte buf[4];
        writeU32Be(increment & 0x7fffffffu, buf);
        return buildFrame(std::to_underlying(FRAME_TYPE::WINDOW_UPDATE), 0, stream_id,
                           std::span<const std::byte>{buf, 4});
    }

    std::string FrameSerializer::serializePing(std::uint64_t opaque, std::uint8_t flags) {
        std::byte buf[8];
        writeU64Be(opaque, buf);
        return buildFrame(std::to_underlying(FRAME_TYPE::PING), flags, 0,
                           std::span<const std::byte>{buf, 8});
    }

    std::string FrameSerializer::serializePingAck(std::uint64_t opaque) {
        return serializePing(opaque, static_cast<std::uint8_t>(FLAGS::ACK));
    }

    std::string FrameSerializer::serializeGoaway(std::uint32_t last_stream_id, std::uint32_t code,
                                                   std::string_view debug) {
        std::vector<std::byte> buf(8 + debug.size());
        writeU32Be(last_stream_id & 0x7fffffffu, buf.data());
        writeU32Be(code, buf.data() + 4);
        if (!debug.empty()) {
            std::memcpy(buf.data() + 8, debug.data(), debug.size());
        }
        return buildFrame(std::to_underlying(FRAME_TYPE::GOAWAY), 0, 0, buf);
    }

    std::string FrameSerializer::serializeRstStream(std::uint32_t stream_id, std::uint32_t code) {
        std::byte buf[4];
        writeU32Be(code, buf);
        return buildFrame(std::to_underlying(FRAME_TYPE::RST_STREAM), 0, stream_id,
                           std::span<const std::byte>{buf, 4});
    }

    std::string FrameSerializer::serializeHeaders(std::uint32_t stream_id,
                                                    std::span<const std::byte> fragment,
                                                    std::uint8_t flags) {
        return buildFrame(std::to_underlying(FRAME_TYPE::HEADERS), flags, stream_id, fragment);
    }

    std::string FrameSerializer::serializeContinuation(std::uint32_t stream_id,
                                                         std::span<const std::byte> fragment,
                                                         std::uint8_t flags) {
        return buildFrame(std::to_underlying(FRAME_TYPE::CONTINUATION), flags, stream_id, fragment);
    }

    std::string FrameSerializer::serializeData(std::uint32_t stream_id,
                                                 std::span<const std::byte> data,
                                                 std::uint8_t flags) {
        return buildFrame(std::to_underlying(FRAME_TYPE::DATA), flags, stream_id, data);
    }

}// namespace usub::unet::http::v2
