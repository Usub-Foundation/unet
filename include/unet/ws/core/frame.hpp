#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace usub::unet::ws {

    enum class OPCODE : uint8_t {
        // Data frames
        CONTINUATION = 0x0,
        TEXT = 0x1,
        BINARY = 0x2,
        // Non-control reserved
        RESERVED_3 = 0x3,
        RESERVED_4 = 0x4,
        RESERVED_5 = 0x5,
        RESERVED_6 = 0x6,
        RESERVED_7 = 0x7,
        // Control frames
        CLOSE = 0x8,
        PING = 0x9,
        PONG = 0xA,
        // Control reserved
        RESERVED_B = 0xB,
        RESERVED_C = 0xC,
        RESERVED_D = 0xD,
        RESERVED_E = 0xE,
        RESERVED_F = 0xF,
    };

    // Codes marked "internal" must never appear in a CLOSE frame on the wire.
    enum class CLOSE_CODE : uint16_t {
        NORMAL = 1000,
        GOING_AWAY = 1001,
        PROTOCOL_ERROR = 1002,
        UNSUPPORTED_DATA = 1003,
        RESERVED = 1004,      // reserved
        NO_STATUS = 1005,     // internal: received CLOSE with no code
        ABNORMAL_CLOSE = 1006,// internal: connection lost without CLOSE frame
        INVALID_PAYLOAD = 1007,
        POLICY_VIOLATION = 1008,
        MESSAGE_TOO_BIG = 1009,
        MANDATORY_EXT = 1010,
        INTERNAL_ERROR = 1011,
        // 1012–1014 reserved by IANA
        IANA_RESERVED_1012 = 1012,
        IANA_RESERVED_1013 = 1012,
        IANA_RESERVED_1014 = 1012,
        TLS_FAILURE = 1015,// internal: TLS handshake failure
    };


    struct FrameHeader {
        bool fin{true};
        bool rsv1{false};
        bool rsv2{false};
        bool rsv3{false};
        OPCODE opcode{};
        bool mask{false};

        uint8_t payload_length{};
        std::variant<std::monostate, std::uint16_t, std::uint64_t> extended_payload_length{};

        std::optional<std::array<std::uint8_t, 4>> masking_key{};

        [[nodiscard]] uint64_t length() const noexcept {
            if (this->payload_length < 126) return this->payload_length;
            if (auto *p = std::get_if<uint16_t>(&this->extended_payload_length)) return *p;
            if (auto *p = std::get_if<uint64_t>(&this->extended_payload_length)) return *p;
            return 0;
        }
    };

    struct Frame : public FrameHeader {
        std::string payload{};
    };

}// namespace usub::unet::ws
