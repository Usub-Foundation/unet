#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace usub::unet::http::v2 {

    enum class FRAME_TYPE : std::uint8_t {
        DATA = 0x00,
        HEADERS = 0x01,
        PRIORITY = 0x02,
        RST_STREAM = 0x03,
        SETTINGS = 0x04,
        PUSH_PROMISE = 0x05,
        PING = 0x06,
        GOAWAY = 0x07,
        WINDOW_UPDATE = 0x08,
        CONTINUATION = 0x09,
        ALTSVC = 0x0a,
        ORIGIN = 0x0c,
        PRIORITY_UPDATE = 0x10
    };

    enum class FLAGS : std::uint8_t {
        END_STREAM = 0x01,
        ACK = 0x01,
        END_HEADERS = 0x04,
        PADDED = 0x08,
        PRIORITY = 0x20,
    };

    enum class SETTINGS : std::uint16_t {
        HEADER_TABLE_SIZE = 0x01,
        ENABLE_PUSH = 0x02,
        MAX_CONCURRENT_STREAMS = 0x03,
        INITIAL_WINDOW_SIZE = 0x04,
        MAX_FRAME_SIZE = 0x05,
        MAX_HEADER_LIST_SIZE = 0x06,
    };

    struct FrameHeader {
        std::uint32_t length{};// 24 bits wide
        FRAME_TYPE type{};
        std::uint8_t flags{};
        std::uint32_t stream_identifier{};// 1st bit is reserved
    };

    struct GenericFrame {
        FrameHeader frame_header;
        std::vector<std::byte> payload;// payload.size() == h.length
    };

    // Data of frames

    struct DataPayload {
        std::uint8_t pad_length{};
        std::vector<std::byte> data{};
        std::vector<std::byte> padding{};
    };

    struct HeadersPayload {
        std::uint8_t pad_length{};

        // PRIORITY fields (obsolete): present only if priority flag is present true
        bool exclusive{false};
        std::uint32_t stream_dependency{};
        std::uint8_t weight{};

        std::vector<std::byte> header_block_fragment;
        std::vector<std::byte> padding;
    };

    struct PriorityPayload {
        bool exclusive{false};
        std::uint32_t stream_dependency{};
        std::uint8_t weight{};
    };

    struct RstStreamPayload {
        std::uint32_t error_code{};
    };

    struct Setting {
        SETTINGS identifier{};
        std::uint32_t value{};
    };

    struct SettingsPayload {
        std::vector<Setting> settings;
    };

    struct PushPromisePayload {
        std::uint8_t pad_length{};
        std::uint32_t promised_stream_id{};
        std::vector<std::byte> header_block_fragment{};
        std::vector<std::byte> padding{};
    };

    struct PingPayload {
        std::uint64_t opaque_data{};
    };

    struct GoAwayPayload {
        std::uint32_t last_stream_id{};
        std::uint32_t error_code{};
        std::vector<std::byte> additional_debug_data{};
    };

    struct WindowUpdatePayload {
        std::uint32_t window_size_increment{};
    };

    struct ContinuationPayload {
        std::vector<std::byte> header_block_fragment{};
    };

    // TODO: Not Implemented
    struct AltSvcPayload {
        std::vector<std::byte> origin{};
        std::vector<std::byte> field_value{};
    };

    struct OriginPayload {
        std::vector<std::vector<std::byte>> origins{};
    };

    struct PriorityUpdatePayload {
        std::uint32_t prioritized_element_id{};
        std::vector<std::byte> priority_field_value{};
    };

    template<class FramePayload>
    struct Frame {
        FrameHeader header;
        FramePayload payload;
    };
}// namespace usub::unet::http::v2
