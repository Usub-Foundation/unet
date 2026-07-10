#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

#include "unet/http/v2/wire/frames.hpp"
#include "unet/http/v2/wire/types.hpp"


namespace usub::unet::http::v2 {

    // Frame-level parser. Pure byte→struct conversion, no state, no I/O.
    class FrameParser {
    public:
        static std::expected<FrameHeader, ERROR_CODE>
        parseFrameHeader(std::span<const std::byte> buf) noexcept;

        static std::expected<DataPayload, ERROR_CODE>
        parseData(const FrameHeader &h, std::span<const std::byte> payload) noexcept;
        static std::expected<HeadersPayload, ERROR_CODE>
        parseHeaders(const FrameHeader &h, std::span<const std::byte> payload) noexcept;
        static std::expected<PriorityPayload, ERROR_CODE>
        parsePriority(const FrameHeader &h, std::span<const std::byte> payload) noexcept;
        static std::expected<RstStreamPayload, ERROR_CODE>
        parseRstStream(const FrameHeader &h, std::span<const std::byte> payload) noexcept;
        static std::expected<SettingsPayload, ERROR_CODE>
        parseSettings(const FrameHeader &h, std::span<const std::byte> payload) noexcept;
        static std::expected<PingPayload, ERROR_CODE>
        parsePing(const FrameHeader &h, std::span<const std::byte> payload) noexcept;
        static std::expected<GoAwayPayload, ERROR_CODE>
        parseGoaway(const FrameHeader &h, std::span<const std::byte> payload) noexcept;
        static std::expected<WindowUpdatePayload, ERROR_CODE>
        parseWindowUpdate(const FrameHeader &h, std::span<const std::byte> payload) noexcept;
        static std::expected<ContinuationPayload, ERROR_CODE>
        parseContinuation(const FrameHeader &h, std::span<const std::byte> payload) noexcept;
    };

}// namespace usub::unet::http::v2
