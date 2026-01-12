#pragma once

#include <array>
#include <cstdint>
#include <string_view>


namespace usub::unet::http {

    extern std::size_t max_headers_size;// chrome uses 256KB as max header size

    enum STATUS_CODE : uint16_t {
        // 1XX Informational
        CONTINUE = 100,
        SWITCHING_PROTOCOLS = 101,
        PROCESSING = 102,
        EARLY_HINTS = 103,

        // 2XX Success
        OK = 200,
        CREATED = 201,
        ACCEPTED = 202,
        NON_AUTHORITATIVE_INFORMATION = 203,
        NO_CONTENT = 204,
        RESET_CONTENT = 205,
        PARTIAL_CONTENT = 206,
        MULTI_STATUS = 207,
        ALREADY_REPORTED = 208,
        IM_USED = 226,

        // 3XX Redirection
        MULTIPLE_CHOICES = 300,
        MOVED_PERMANENTLY = 301,
        FOUND = 302,
        SEE_OTHER = 303,
        NOT_MODIFIED = 304,
        USE_PROXY = 305,
        TEMPORARY_REDIRECT = 307,
        PERMANENT_REDIRECT = 308,

        // 4XX Client Errors
        BAD_REQUEST = 400,
        UNAUTHORIZED = 401,
        PAYMENT_REQUIRED = 402,
        FORBIDDEN = 403,
        NOT_FOUND = 404,
        METHOD_NOT_ALLOWED = 405,
        NOT_ACCEPTABLE = 406,
        PROXY_AUTHENTICATION_REQUIRED = 407,
        REQUEST_TIMEOUT = 408,
        CONFLICT = 409,
        GONE = 410,
        LENGTH_REQUIRED = 411,
        PRECONDITION_FAILED = 412,
        PAYLOAD_TOO_LARGE = 413,
        URI_TOO_LONG = 414,
        UNSUPPORTED_MEDIA_TYPE = 415,
        RANGE_NOT_SATISFIABLE = 416,
        EXPECTATION_FAILED = 417,
        IM_A_TEAPOT = 418,
        AUTHENTICATION_TIMEOUT = 419,// non-standard
        MISDIRECTED_REQUEST = 421,
        UNPROCESSABLE_ENTITY = 422,
        LOCKED = 423,
        FAILED_DEPENDENCY = 424,
        TOO_EARLY = 425,
        UPGRADE_REQUIRED = 426,
        PRECONDITION_REQUIRED = 428,
        TOO_MANY_REQUESTS = 429,
        REQUEST_HEADER_FIELDS_TOO_LARGE = 431,
        UNAVAILABLE_FOR_LEGAL_REASONS = 451,
        CLIENT_CLOSED_REQUEST = 499,// nginx

        // 5XX Server Errors
        INTERNAL_SERVER_ERROR = 500,
        NOT_IMPLEMENTED = 501,
        BAD_GATEWAY = 502,
        SERVICE_UNAVAILABLE = 503,
        GATEWAY_TIMEOUT = 504,
        HTTP_VERSION_NOT_SUPPORTED = 505,
        VARIANT_ALSO_NEGOTIATES = 506,
        INSUFFICIENT_STORAGE = 507,
        LOOP_DETECTED = 508,
        NOT_EXTENDED = 510,
        NETWORK_AUTHENTICATION_REQUIRED = 511,

        // Non-RFC (Cloudflare etc.)
        UNKNOWN_ERROR = 520,
        WEB_SERVER_IS_DOWN = 521,
        CONNECTION_TIMED_OUT = 522,
        ORIGIN_IS_UNREACHABLE = 523,
        A_TIMEOUT_OCCURRED = 524,
        SSL_HANDSHAKE_FAILED = 525,
        INVALID_SSL_CERTIFICATE = 526,
    };

    static constexpr std::array<std::string_view, 600> status_messages = [] {
        std::array<std::string_view, 600> arr{};

        arr[CONTINUE] = "Continue";
        arr[SWITCHING_PROTOCOLS] = "Switching Protocols";
        arr[PROCESSING] = "Processing";
        arr[EARLY_HINTS] = "Early Hints";

        arr[OK] = "OK";
        arr[CREATED] = "Created";
        arr[ACCEPTED] = "Accepted";
        arr[NON_AUTHORITATIVE_INFORMATION] = "Non-Authoritative Information";
        arr[NO_CONTENT] = "No Content";
        arr[RESET_CONTENT] = "Reset Content";
        arr[PARTIAL_CONTENT] = "Partial Content";
        arr[MULTI_STATUS] = "Multi-Status";
        arr[ALREADY_REPORTED] = "Already Reported";
        arr[IM_USED] = "IM Used";

        arr[MULTIPLE_CHOICES] = "Multiple Choices";
        arr[MOVED_PERMANENTLY] = "Moved Permanently";
        arr[FOUND] = "Found";
        arr[SEE_OTHER] = "See Other";
        arr[NOT_MODIFIED] = "Not Modified";
        arr[USE_PROXY] = "Use Proxy";
        arr[TEMPORARY_REDIRECT] = "Temporary Redirect";
        arr[PERMANENT_REDIRECT] = "Permanent Redirect";

        arr[BAD_REQUEST] = "Bad Request";
        arr[UNAUTHORIZED] = "Unauthorized";
        arr[PAYMENT_REQUIRED] = "Payment Required";
        arr[FORBIDDEN] = "Forbidden";
        arr[NOT_FOUND] = "Not Found";
        arr[METHOD_NOT_ALLOWED] = "Method Not Allowed";
        arr[NOT_ACCEPTABLE] = "Not Acceptable";
        arr[PROXY_AUTHENTICATION_REQUIRED] = "Proxy Authentication Required";
        arr[REQUEST_TIMEOUT] = "Request Timeout";
        arr[CONFLICT] = "Conflict";
        arr[GONE] = "Gone";
        arr[LENGTH_REQUIRED] = "Length Required";
        arr[PRECONDITION_FAILED] = "Precondition Failed";
        arr[PAYLOAD_TOO_LARGE] = "Payload Too Large";
        arr[URI_TOO_LONG] = "URI Too Long";
        arr[UNSUPPORTED_MEDIA_TYPE] = "Unsupported Media Type";
        arr[RANGE_NOT_SATISFIABLE] = "Range Not Satisfiable";
        arr[EXPECTATION_FAILED] = "Expectation Failed";
        arr[IM_A_TEAPOT] = "I'm a teapot";
        arr[AUTHENTICATION_TIMEOUT] = "Authentication Timeout";
        arr[MISDIRECTED_REQUEST] = "Misdirected Request";
        arr[UNPROCESSABLE_ENTITY] = "Unprocessable Entity";
        arr[LOCKED] = "Locked";
        arr[FAILED_DEPENDENCY] = "Failed Dependency";
        arr[TOO_EARLY] = "Too Early";
        arr[UPGRADE_REQUIRED] = "Upgrade Required";
        arr[PRECONDITION_REQUIRED] = "Precondition Required";
        arr[TOO_MANY_REQUESTS] = "Too Many Requests";
        arr[REQUEST_HEADER_FIELDS_TOO_LARGE] = "Request Header Fields Too Large";
        arr[UNAVAILABLE_FOR_LEGAL_REASONS] = "Unavailable For Legal Reasons";
        arr[CLIENT_CLOSED_REQUEST] = "Client Closed Request";

        arr[INTERNAL_SERVER_ERROR] = "Internal Server Error";
        arr[NOT_IMPLEMENTED] = "Not Implemented";
        arr[BAD_GATEWAY] = "Bad Gateway";
        arr[SERVICE_UNAVAILABLE] = "Service Unavailable";
        arr[GATEWAY_TIMEOUT] = "Gateway Timeout";
        arr[HTTP_VERSION_NOT_SUPPORTED] = "HTTP Version Not Supported";
        arr[VARIANT_ALSO_NEGOTIATES] = "Variant Also Negotiates";
        arr[INSUFFICIENT_STORAGE] = "Insufficient Storage";
        arr[LOOP_DETECTED] = "Loop Detected";
        arr[NOT_EXTENDED] = "Not Extended";
        arr[NETWORK_AUTHENTICATION_REQUIRED] = "Network Authentication Required";

        arr[UNKNOWN_ERROR] = "Unknown Error";
        arr[WEB_SERVER_IS_DOWN] = "Web Server Is Down";
        arr[CONNECTION_TIMED_OUT] = "Connection Timed Out";
        arr[ORIGIN_IS_UNREACHABLE] = "Origin Is Unreachable";
        arr[A_TIMEOUT_OCCURRED] = "A Timeout Occurred";
        arr[SSL_HANDSHAKE_FAILED] = "SSL Handshake Failed";
        arr[INVALID_SSL_CERTIFICATE] = "Invalid SSL Certificate";

        return arr;
    }();

    enum class FRAMING_POLICY : uint8_t {
        ALL = 0,
        CONTENT_LENGTH_ONLY = 1,
        CHUNKED_ONLY = 2,
    };

    struct MessagePolicy {
        static const FRAMING_POLICY framing =
                FRAMING_POLICY::ALL;             // Allow both chunked and content-length // NO SUPPORT YET
        static const bool allow_trailers = false;// Disallow trailers for now // NO SUPPORT FOR TRAILERS YET
        std::size_t max_body_size = 8 * 1024 * 1024;
    };

    enum class VERSION : uint8_t {
        HTTP_0_9 = 9,
        HTTP_1_0 = 10,
        HTTP_1_1 = 11,
        HTTP_2_0 = 20,
        HTTP_3_0 = 30,
    };

}// namespace usub::unet::http
