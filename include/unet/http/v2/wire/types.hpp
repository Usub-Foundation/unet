#pragma once

#include <cstdint>
#include <string_view>

// winerror.h `#define NO_ERROR 0L` collides with the ERROR_CODE enumerator below.
#ifdef NO_ERROR
#  undef NO_ERROR
#endif


namespace usub::unet::http::v2 {

    // RFC 9113 §3.4.
    inline constexpr std::string_view h2_preface{"PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"};


    // RFC 9113 + ALTSVC (RFC 7838), ORIGIN (RFC 8336), PRIORITY_UPDATE (RFC 9218).
    enum class FRAME_TYPE : std::uint8_t {
        DATA            = 0x00,
        HEADERS         = 0x01,
        PRIORITY        = 0x02,
        RST_STREAM      = 0x03,
        SETTINGS        = 0x04,
        PUSH_PROMISE    = 0x05,
        PING            = 0x06,
        GOAWAY          = 0x07,
        WINDOW_UPDATE   = 0x08,
        CONTINUATION    = 0x09,
        ALTSVC          = 0x0a,
        ORIGIN          = 0x0c,
        PRIORITY_UPDATE = 0x10,
    };

    // Error codes per RFC 9113 §7.
    enum class ERROR_CODE : std::uint32_t {
        NO_ERROR            = 0x00,
        PROTOCOL_ERROR      = 0x01,
        INTERNAL_ERROR      = 0x02,
        FLOW_CONTROL_ERROR  = 0x03,
        SETTINGS_TIMEOUT    = 0x04,
        STREAM_CLOSED       = 0x05,
        FRAME_SIZE_ERROR    = 0x06,
        REFUSED_STREAM      = 0x07,
        CANCEL              = 0x08,
        COMPRESSION_ERROR   = 0x09,
        CONNECT_ERROR       = 0x0a,
        ENHANCE_YOUR_CALM   = 0x0b,
        INADEQUATE_SECURITY = 0x0c,
        HTTP_1_1_REQUIRED   = 0x0d,
    };

}// namespace usub::unet::http::v2
