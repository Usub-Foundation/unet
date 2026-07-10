#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "unet/http/v2/wire/types.hpp"


namespace usub::unet::http::v2 {

    inline constexpr std::size_t frame_header_size = 9;

    struct FrameHeader {
        std::uint32_t length{};
        std::uint8_t  type{};
        std::uint8_t  flags{};
        std::uint32_t stream_id{};
    };

    struct GenericFrame {
        FrameHeader            header{};
        std::vector<std::byte> payload{};
    };

    struct DataPayload {
        std::uint8_t           pad_length{};
        std::vector<std::byte> data{};
        std::vector<std::byte> padding{};
    };

    struct HeadersPayload {
        std::uint8_t  pad_length{};
        bool          has_priority{false};
        bool          exclusive{false};
        std::uint32_t stream_dependency{};
        std::uint8_t  weight{};
        std::vector<std::byte> header_block_fragment{};
        std::vector<std::byte> padding{};
    };

    struct PriorityPayload {
        bool          exclusive{false};
        std::uint32_t stream_dependency{};
        std::uint8_t  weight{};
    };

    struct RstStreamPayload {
        std::uint32_t error_code{};
    };

    struct Setting {
        std::uint16_t id{};
        std::uint32_t value{};
    };

    struct SettingsPayload {
        std::vector<Setting> settings;
    };

    struct PushPromisePayload {
        std::uint8_t  pad_length{};
        std::uint32_t promised_stream_id{};
        std::vector<std::byte> header_block_fragment{};
        std::vector<std::byte> padding{};
    };

    struct PingPayload {
        std::uint64_t opaque_data{};
    };

    struct GoAwayPayload {
        std::uint32_t          last_stream_id{};
        std::uint32_t          error_code{};
        std::vector<std::byte> additional_debug_data{};
    };

    struct WindowUpdatePayload {
        std::uint32_t window_size_increment{};
    };

    struct ContinuationPayload {
        std::vector<std::byte> header_block_fragment{};
    };

    struct AltSvcPayload {
        std::vector<std::byte> origin{};
        std::vector<std::byte> field_value{};
    };

    struct OriginPayload {
        std::vector<std::vector<std::byte>> origins{};
    };

    struct PriorityUpdatePayload {
        std::uint32_t          prioritized_element_id{};
        std::vector<std::byte> priority_field_value{};
    };

}// namespace usub::unet::http::v2
