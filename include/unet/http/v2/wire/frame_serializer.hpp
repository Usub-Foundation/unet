#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "unet/http/v2/wire/frames.hpp"
#include "unet/http/v2/wire/types.hpp"


namespace usub::unet::http::v2 {

    // Frame-level serializer. Pure struct→byte conversion, no state, no I/O.
    class FrameSerializer {
    public:
        static void writeFrameHeader(const FrameHeader &header, std::span<std::byte> out) noexcept;
        static std::string writeFrameHeaderString(const FrameHeader &header);

        static std::string serializeFrame(const FrameHeader &header,
                                            std::span<const std::byte> payload);

        static std::string serializeSettings(const SettingsPayload &p, std::uint8_t flags = 0);
        static std::string serializeSettingsAck();
        static std::string serializeWindowUpdate(std::uint32_t stream_id, std::uint32_t increment);
        static std::string serializePing(std::uint64_t opaque, std::uint8_t flags = 0);
        static std::string serializePingAck(std::uint64_t opaque);
        static std::string serializeGoaway(std::uint32_t last_stream_id, std::uint32_t code,
                                            std::string_view debug = {});
        static std::string serializeRstStream(std::uint32_t stream_id, std::uint32_t code);
        static std::string serializeHeaders(std::uint32_t stream_id,
                                              std::span<const std::byte> fragment,
                                              std::uint8_t flags);
        static std::string serializeContinuation(std::uint32_t stream_id,
                                                   std::span<const std::byte> fragment,
                                                   std::uint8_t flags);
        static std::string serializeData(std::uint32_t stream_id,
                                           std::span<const std::byte> data,
                                           std::uint8_t flags);
    };

}// namespace usub::unet::http::v2
