#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <utility>

#include "unet/http/v2/wire/types.hpp"


namespace usub::unet::http::v2 {

    // RFC 9113 §6.5.2 + IANA HTTP/2 Settings registry.
    enum class SETTINGS : std::uint16_t {
        HEADER_TABLE_SIZE       = 0x01,
        ENABLE_PUSH             = 0x02,
        MAX_CONCURRENT_STREAMS  = 0x03,
        INITIAL_WINDOW_SIZE     = 0x04,
        MAX_FRAME_SIZE          = 0x05,
        MAX_HEADER_LIST_SIZE    = 0x06,
        ENABLE_CONNECT_PROTOCOL = 0x08,// RFC 8441 §3.
        NO_RFC7540_PRIORITIES   = 0x09,// RFC 9218 §2.1.
    };

    struct Settings {
        std::uint32_t header_table_size{4096};
        std::uint32_t enable_push{1};
        std::optional<std::uint32_t> max_concurrent_streams{};
        std::uint32_t initial_window_size{65535};
        std::uint32_t max_frame_size{16384};
        std::optional<std::uint32_t> max_header_list_size{};
        std::uint32_t enable_connect_protocol{0};
        std::uint32_t no_rfc7540_priorities{0};
        std::unordered_map<std::uint16_t, std::uint32_t> custom{};

        // False on values that demand a connection error.
        constexpr bool apply(std::uint16_t id, std::uint32_t value) noexcept {
            switch (id) {
                case std::to_underlying(SETTINGS::HEADER_TABLE_SIZE):
                    header_table_size = value;
                    return true;
                case std::to_underlying(SETTINGS::ENABLE_PUSH):
                    if (value > 1) return false;
                    enable_push = value;
                    return true;
                case std::to_underlying(SETTINGS::MAX_CONCURRENT_STREAMS):
                    max_concurrent_streams = value;
                    return true;
                case std::to_underlying(SETTINGS::INITIAL_WINDOW_SIZE):
                    if (value > 0x7fffffff) return false;
                    initial_window_size = value;
                    return true;
                case std::to_underlying(SETTINGS::MAX_FRAME_SIZE):
                    if (value < 16384 || value > 16777215) return false;
                    max_frame_size = value;
                    return true;
                case std::to_underlying(SETTINGS::MAX_HEADER_LIST_SIZE):
                    max_header_list_size = value;
                    return true;
                case std::to_underlying(SETTINGS::ENABLE_CONNECT_PROTOCOL):
                    if (value > 1) return false;
                    enable_connect_protocol = value;
                    return true;
                case std::to_underlying(SETTINGS::NO_RFC7540_PRIORITIES):
                    if (value > 1) return false;
                    no_rfc7540_priorities = value;
                    return true;
                default:
                    // RFC 9113 §6.5.3 — accept unknown IDs silently.
                    if (id >= 0x01 && id <= 0x0a) return true;
                    custom[id] = value;
                    return true;
            }
        }
    };

}// namespace usub::unet::http::v2
