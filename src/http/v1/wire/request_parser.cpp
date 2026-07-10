#include "unet/http/v1/wire/request_parser.hpp"

#include <charconv>

// TODO: Recheck on all that uses ctx....

namespace usub::unet::http::v1 {
    namespace {

        inline std::uint8_t hex_value(unsigned char c) {
            if (c >= '0' && c <= '9') return static_cast<std::uint8_t>(c - '0');
            if (c >= 'a' && c <= 'f') return static_cast<std::uint8_t>(10 + (c - 'a'));
            return static_cast<std::uint8_t>(10 + (c - 'A'));
        }

        inline std::string_view trim_ows(std::string_view value) {
            std::size_t start = 0;
            std::size_t end = value.size();
            while (start < end && (value[start] == ' ' || value[start] == '\t')) { ++start; }
            while (end > start && (value[end - 1] == ' ' || value[end - 1] == '\t')) { --end; }
            return value.substr(start, end - start);
        }

        inline bool parse_uint(std::string_view value, std::size_t &out) {
            if (value.empty()) return false;
            const char *const begin = value.data();
            const char *const end = begin + value.size();
            const auto [ptr, ec] = std::from_chars(begin, end, out);
            return ec == std::errc{} && ptr == end;
        }

        inline bool is_chunked_token(std::string_view token) {
            constexpr std::string_view chunked = "chunked";
            if (token.size() != chunked.size()) return false;
            for (std::size_t i = 0; i < chunked.size(); ++i) {
                if (asciiLower(token[i]) != chunked[i]) return false;
            }
            return true;
        }

        inline bool parse_hex_size(std::string_view value, std::size_t &out) {
            if (value.empty()) return false;
            std::size_t parsed = 0;
            for (const unsigned char ch: value) {
                if (!isHexDigit(ch)) return false;
                const std::size_t digit = hex_value(ch);
                if (parsed > (std::numeric_limits<std::size_t>::max() - digit) / 16) { return false; }
                parsed = parsed * 16 + digit;
            }
            out = parsed;
            return true;
        }

    }// namespace

    std::expected<void, ParseError> RequestParser::step(RequestReader &request, std::string_view::const_iterator &begin,
                                                        const std::string_view::const_iterator end) {
        auto &ctx = this->context_;
        auto &state = ctx.state;
        using Status = usub::unet::http::STATUS_CODE;

        // HEADERS_DONE is here because complete can be w/o body
        if (state == STATE::HEADERS_DONE) {
            state = ctx.body_type == BODY_TYPE::CHUNKED          ? STATE::DATA_CHUNKED_SIZE
                    : ctx.body_type == BODY_TYPE::CONTENT_LENGTH ? STATE::DATA_CONTENT_LENGTH
                                                                 : STATE::COMPLETE;
        }

        while (begin != end) {
            switch (state) {
                case STATE::METHOD_TOKEN: {
                    auto &method = request.metadata.method_token;
                    // TODO: im gonna leave it here for now, because it's
                    if (method.empty()) {
                        ctx.headers_size = 0;
                        ctx.body_bytes_read = 0;
                        // ctx.chunk_bytes_read = 0;
                        ctx.current_state_size = 0;
                        ctx.kv_buffer = {};
                        request.metadata.uri = {};
                        request.headers = {};
                    }
                    while (begin != end) {
                        if (isTchar(*begin)) [[likely]] {
                            method.push_back(static_cast<char>(*begin));
                            ++begin;
                            ++ctx.current_state_size;
                        } else if (*begin == ' ') {
                            if (method.empty()) {
                                state = STATE::FAILED;
                                return std::unexpected(ParseError{
                                    .code            = ParseError::CODE::GENERIC_ERROR,
                                    .expected_status = Status::BAD_REQUEST,
                                    .message         = "Empty method token",
                                });
                            }
                            ++begin;
                            state = STATE::URI;
                            ctx.current_state_size = 0;
                            break;
                        } else {
                            state = STATE::FAILED;
                            return std::unexpected(ParseError{
                                .code            = ParseError::CODE::GENERIC_ERROR,
                                .expected_status = Status::BAD_REQUEST,
                                .message         = "Invalid method token",
                            });
                        }
                        if (ctx.current_state_size > max_method_token_size) {
                            state = STATE::FAILED;
                            return std::unexpected(ParseError{
                                .code            = ParseError::CODE::GENERIC_ERROR,
                                .expected_status = Status::BAD_REQUEST,
                                .message         = "Method token too big",
                            });
                        }
                    }
                    break;
                }
                case STATE::URI: {
                    if (begin == end) break;

                    const unsigned char c = static_cast<unsigned char>(*begin);

                    if (c == '/') {
                        if (request.metadata.method_token == "CONNECT") {
                            state = STATE::FAILED;
                            return std::unexpected(ParseError{
                                .code            = ParseError::CODE::GENERIC_ERROR,
                                .expected_status = Status::BAD_REQUEST,
                                .message         = "CONNECT requires authority-form",
                            });
                        }
                        state = STATE::ORIGIN_PATH;
                    } else if (c == '*') {
                        if (request.metadata.method_token != "OPTIONS") {
                            state = STATE::FAILED;
                            return std::unexpected(ParseError{
                                .code            = ParseError::CODE::GENERIC_ERROR,
                                .expected_status = Status::BAD_REQUEST,
                                .message         = "asterisk-form reserved for OPTIONS",
                            });
                        }
                        state = STATE::ASTERISK_FORM;
                    } else if (request.metadata.method_token == "CONNECT") {
                        if (!isHostChar(c) && c != '[') {
                            state = STATE::FAILED;
                            return std::unexpected(ParseError{
                                .code            = ParseError::CODE::GENERIC_ERROR,
                                .expected_status = Status::BAD_REQUEST,
                                .message         = "invalid authority-form host",
                            });
                        }
                        state = STATE::AUTHORITY_FORM;
                    } else if (isAlpha(c)) {
                        state = STATE::ABSOLUTE_FORM;
                    } else {
                        state = STATE::FAILED;
                        return std::unexpected(ParseError{
                            .code            = ParseError::CODE::GENERIC_ERROR,
                            .expected_status = Status::BAD_REQUEST,
                            .message         = "Unknown URI type",
                        });
                    }
                    break;
                }
                case STATE::ORIGIN_PATH: {
                    auto &path = request.metadata.uri.path;
                    while (begin != end) {
                        if (isPathChar(*begin)) {
                            path.push_back(static_cast<char>(*begin));
                            ++begin;
                            ++ctx.current_state_size;
                            continue;
                        } else if (*begin == '?') {
                            state = STATE::ORIGIN_QUERY;
                            ++begin;
                            ++ctx.current_state_size;
                            break;
                        } else if (*begin == '#') {
                            state = STATE::ORIGIN_FRAGMENT;
                            ++begin;
                            ++ctx.current_state_size;
                            break;
                        } else if (*begin == ' ') {
                            state = STATE::VERSION;
                            ++begin;
                            ctx.current_state_size = 0;
                            break;
                        } else if (*begin == '\r') {
                            state = STATE::FAILED;
                            return std::unexpected(ParseError{
                                .code            = ParseError::CODE::GENERIC_ERROR,
                                .expected_status = Status::BAD_REQUEST,
                                .message         = "HTTP/0.9 not supported yet",
                            });
                        } else {
                            state = STATE::FAILED;
                            return std::unexpected(ParseError{
                                .code            = ParseError::CODE::GENERIC_ERROR,
                                .expected_status = Status::BAD_REQUEST,
                                .message         = "Invalid path character",
                            });
                        }
                        if (ctx.current_state_size >= usub::unet::http::max_uri_size) {
                            state = STATE::FAILED;
                            return std::unexpected(ParseError{
                                .code            = ParseError::CODE::GENERIC_ERROR,
                                .expected_status = Status::URI_TOO_LONG,
                                .message         = "URI too long",
                            });
                        }
                    }
                    break;
                }
                case STATE::ORIGIN_QUERY: {
                    auto &query = request.metadata.uri.query;
                    while (begin != end) {
                        if (isQueryChar(*begin)) {
                            query.push_back(static_cast<char>(*begin));
                            ++begin;
                            ++ctx.current_state_size;
                            continue;
                        } else if (*begin == '#') {
                            state = STATE::ORIGIN_FRAGMENT;
                            ++begin;
                            ++ctx.current_state_size;
                            break;
                        } else if (*begin == ' ') {
                            state = STATE::VERSION;
                            ++begin;
                            ctx.current_state_size = 0;
                            break;
                        } else {
                            state = STATE::FAILED;
                            return std::unexpected(ParseError{
                                .code            = ParseError::CODE::GENERIC_ERROR,
                                .expected_status = Status::BAD_REQUEST,
                                .message         = "Invalid query character",
                            });
                        }
                        if (ctx.current_state_size >= usub::unet::http::max_uri_size) {
                            state = STATE::FAILED;
                            return std::unexpected(ParseError{
                                .code            = ParseError::CODE::GENERIC_ERROR,
                                .expected_status = Status::URI_TOO_LONG,
                                .message         = "URI too long",
                            });
                        }
                    }
                    break;
                }
                case STATE::ORIGIN_FRAGMENT: {
                    auto &fragment = request.metadata.uri.fragment;
                    while (begin != end) {
                        if (isQueryChar(*begin)) {// fragment grammar == query grammar (RFC 3986 §3.5)
                            fragment.push_back(static_cast<char>(*begin));
                            ++begin;
                            ++ctx.current_state_size;
                            if (ctx.current_state_size >= usub::unet::http::max_uri_size) {
                                state = STATE::FAILED;
                                return std::unexpected(ParseError{
                                    .code            = ParseError::CODE::GENERIC_ERROR,
                                    .expected_status = Status::URI_TOO_LONG,
                                    .message         = "URI too long",
                                });
                            }
                            continue;
                        } else if (*begin == ' ') {
                            state = STATE::VERSION;
                            ++begin;
                            ctx.current_state_size = 0;
                            break;
                        } else {
                            state = STATE::FAILED;
                            return std::unexpected(ParseError{
                                .code            = ParseError::CODE::GENERIC_ERROR,
                                .expected_status = Status::BAD_REQUEST,
                                .message         = "Invalid fragment character",
                            });
                        }
                    }
                    break;
                }
                case STATE::ABSOLUTE_FORM: {
                    auto &scheme = request.metadata.uri.scheme;
                    while (begin != end) {
                        if (isSchemeChar(*begin)) {
                            scheme.push_back(asciiLower(*begin));
                            ++begin;
                            ++ctx.current_state_size;
                            if (ctx.current_state_size >= usub::unet::http::max_uri_size) {
                                state = STATE::FAILED;
                                return std::unexpected(ParseError{
                                    .code            = ParseError::CODE::GENERIC_ERROR,
                                    .expected_status = Status::URI_TOO_LONG,
                                    .message         = "URI too long",
                                });
                            }
                            continue;
                        } else if (*begin == ':') {
                            if (scheme.empty()) {
                                state = STATE::FAILED;
                                return std::unexpected(ParseError{
                                    .code            = ParseError::CODE::GENERIC_ERROR,
                                    .expected_status = Status::BAD_REQUEST,
                                    .message         = "Empty scheme",
                                });
                            }
                            ++begin;
                            ++ctx.current_state_size;
                            state = STATE::ABSOLUTE_FORM_AFTER_SCHEME;
                            break;
                        } else {
                            state = STATE::FAILED;
                            return std::unexpected(ParseError{
                                .code            = ParseError::CODE::GENERIC_ERROR,
                                .expected_status = Status::BAD_REQUEST,
                                .message         = "Invalid scheme character",
                            });
                        }
                    }
                    break;
                }
                case STATE::ABSOLUTE_FORM_AFTER_SCHEME: {
                    if (begin == end) break;
                    if (*begin != '/') {
                        state = STATE::FAILED;
                        return std::unexpected(ParseError{
                            .code            = ParseError::CODE::GENERIC_ERROR,
                            .expected_status = Status::BAD_REQUEST,
                            .message         = "Absolute form expects '//' after ':'",
                        });
                    }
                    ++begin;
                    ++ctx.current_state_size;
                    state = STATE::ABSOLUTE_FORM_AFTER_FIRST_SLASH;
                    break;
                }
                case STATE::ABSOLUTE_FORM_AFTER_FIRST_SLASH: {
                    if (begin == end) break;
                    if (*begin != '/') {
                        state = STATE::FAILED;
                        return std::unexpected(ParseError{
                            .code            = ParseError::CODE::GENERIC_ERROR,
                            .expected_status = Status::BAD_REQUEST,
                            .message         = "Absolute form expects '//' after ':'",
                        });
                    }
                    ++begin;
                    ++ctx.current_state_size;
                    state = STATE::ABSOLUTE_FORM_HOST;
                    break;
                }
                case STATE::ABSOLUTE_FORM_HOST: {
                    if (begin == end) break;
                    if (*begin == '[') {
                        ++begin;
                        ++ctx.current_state_size;
                        state = STATE::ABSOLUTE_FORM_HOST_IPV6;
                        break;
                    }
                    auto &host = request.metadata.uri.authority.host;
                    while (begin != end) {
                        if (isHostChar(*begin)) {
                            host.push_back(asciiLower(*begin));
                            ++begin;
                            ++ctx.current_state_size;
                            if (ctx.current_state_size >= usub::unet::http::max_uri_size) {
                                state = STATE::FAILED;
                                return std::unexpected(ParseError{
                                    .code            = ParseError::CODE::GENERIC_ERROR,
                                    .expected_status = Status::URI_TOO_LONG,
                                    .message         = "URI too long",
                                });
                            }
                            continue;
                        } else if (*begin == ':') {
                            ++begin;
                            ++ctx.current_state_size;
                            state = STATE::ABSOLUTE_FORM_PORT;
                            break;
                        } else if (*begin == '/') {
                            // leave the '/' for ORIGIN_PATH to consume
                            state = STATE::ORIGIN_PATH;
                            break;
                        } else if (*begin == '?') {
                            ++begin;
                            ++ctx.current_state_size;
                            state = STATE::ORIGIN_QUERY;
                            break;
                        } else if (*begin == '#') {
                            ++begin;
                            ++ctx.current_state_size;
                            state = STATE::ORIGIN_FRAGMENT;
                            break;
                        } else if (*begin == ' ') {
                            ++begin;
                            ctx.current_state_size = 0;
                            state = STATE::VERSION;
                            break;
                        } else {
                            state = STATE::FAILED;
                            return std::unexpected(ParseError{
                                .code            = ParseError::CODE::GENERIC_ERROR,
                                .expected_status = Status::BAD_REQUEST,
                                .message         = "Invalid host character",
                            });
                        }
                    }
                    break;
                }
                case STATE::ABSOLUTE_FORM_HOST_IPV6: {
                    auto &host = request.metadata.uri.authority.host;
                    while (begin != end) {
                        const unsigned char c = static_cast<unsigned char>(*begin);
                        if (isHexDigit(c) || c == ':' || c == '.' || c == '%') {
                            host.push_back(static_cast<char>(c));
                            ++begin;
                            ++ctx.current_state_size;
                            if (ctx.current_state_size >= usub::unet::http::max_uri_size) {
                                state = STATE::FAILED;
                                return std::unexpected(ParseError{
                                    .code            = ParseError::CODE::GENERIC_ERROR,
                                    .expected_status = Status::URI_TOO_LONG,
                                    .message         = "URI too long",
                                });
                            }
                            continue;
                        } else if (c == ']') {
                            ++begin;
                            ++ctx.current_state_size;
                            state = STATE::ABSOLUTE_FORM_AFTER_HOST;
                            break;
                        } else {
                            state = STATE::FAILED;
                            return std::unexpected(ParseError{
                                .code            = ParseError::CODE::GENERIC_ERROR,
                                .expected_status = Status::BAD_REQUEST,
                                .message         = "Invalid IPv6 host character",
                            });
                        }
                    }
                    break;
                }
                case STATE::ABSOLUTE_FORM_AFTER_HOST: {
                    if (begin == end) break;
                    if (*begin == ':') {
                        ++begin;
                        ++ctx.current_state_size;
                        state = STATE::ABSOLUTE_FORM_PORT;
                    } else if (*begin == '/') {
                        // leave for ORIGIN_PATH
                        state = STATE::ORIGIN_PATH;
                    } else if (*begin == '?') {
                        ++begin;
                        ++ctx.current_state_size;
                        state = STATE::ORIGIN_QUERY;
                    } else if (*begin == '#') {
                        ++begin;
                        ++ctx.current_state_size;
                        state = STATE::ORIGIN_FRAGMENT;
                    } else if (*begin == ' ') {
                        ++begin;
                        ctx.current_state_size = 0;
                        state = STATE::VERSION;
                    } else {
                        state = STATE::FAILED;
                        return std::unexpected(ParseError{
                            .code            = ParseError::CODE::GENERIC_ERROR,
                            .expected_status = Status::BAD_REQUEST,
                            .message         = "Invalid char after host",
                        });
                    }
                    break;
                }
                case STATE::ABSOLUTE_FORM_PORT: {
                    auto &port = request.metadata.uri.authority.port;
                    while (begin != end) {
                        if (isDigit(*begin)) {
                            std::uint32_t p = port * 10u + static_cast<std::uint32_t>(*begin - '0');
                            if (p > 65535u) {
                                state = STATE::FAILED;
                                return std::unexpected(ParseError{
                                    .code            = ParseError::CODE::GENERIC_ERROR,
                                    .expected_status = Status::BAD_REQUEST,
                                    .message         = "Port out of range",
                                });
                            }
                            port = static_cast<std::uint16_t>(p);
                            ++begin;
                            ++ctx.current_state_size;
                            if (ctx.current_state_size >= usub::unet::http::max_uri_size) {
                                state = STATE::FAILED;
                                return std::unexpected(ParseError{
                                    .code            = ParseError::CODE::GENERIC_ERROR,
                                    .expected_status = Status::URI_TOO_LONG,
                                    .message         = "URI too long",
                                });
                            }
                            continue;
                        } else if (*begin == '/') {
                            // leave for ORIGIN_PATH
                            state = STATE::ORIGIN_PATH;
                            break;
                        } else if (*begin == '?') {
                            ++begin;
                            ++ctx.current_state_size;
                            state = STATE::ORIGIN_QUERY;
                            break;
                        } else if (*begin == '#') {
                            ++begin;
                            ++ctx.current_state_size;
                            state = STATE::ORIGIN_FRAGMENT;
                            break;
                        } else if (*begin == ' ') {
                            ++begin;
                            ctx.current_state_size = 0;
                            state = STATE::VERSION;
                            break;
                        } else {
                            state = STATE::FAILED;
                            return std::unexpected(ParseError{
                                .code            = ParseError::CODE::GENERIC_ERROR,
                                .expected_status = Status::BAD_REQUEST,
                                .message         = "Invalid port character",
                            });
                        }
                    }
                    break;
                }
                case STATE::AUTHORITY_FORM: {
                    if (begin == end) break;
                    if (*begin == '[') {
                        ++begin;
                        ++ctx.current_state_size;
                        state = STATE::AUTHORITY_FORM_HOST_IPV6;
                        break;
                    }
                    auto &host = request.metadata.uri.authority.host;
                    while (begin != end) {
                        if (isHostChar(*begin)) {
                            host.push_back(asciiLower(*begin));
                            ++begin;
                            ++ctx.current_state_size;
                            if (ctx.current_state_size >= usub::unet::http::max_uri_size) {
                                state = STATE::FAILED;
                                return std::unexpected(ParseError{
                                    .code            = ParseError::CODE::GENERIC_ERROR,
                                    .expected_status = Status::URI_TOO_LONG,
                                    .message         = "URI too long",
                                });
                            }
                            continue;
                        } else if (*begin == ':') {
                            ++begin;
                            ++ctx.current_state_size;
                            state = STATE::AUTHORITY_FORM_PORT;
                            break;
                        } else {
                            state = STATE::FAILED;
                            return std::unexpected(ParseError{
                                .code            = ParseError::CODE::GENERIC_ERROR,
                                .expected_status = Status::BAD_REQUEST,
                                .message         = "Invalid authority host character",
                            });
                        }
                    }
                    break;
                }
                case STATE::AUTHORITY_FORM_HOST_IPV6: {
                    auto &host = request.metadata.uri.authority.host;
                    while (begin != end) {
                        const unsigned char c = static_cast<unsigned char>(*begin);
                        if (isHexDigit(c) || c == ':' || c == '.' || c == '%') {
                            host.push_back(static_cast<char>(c));
                            ++begin;
                            ++ctx.current_state_size;
                            if (ctx.current_state_size >= usub::unet::http::max_uri_size) {
                                state = STATE::FAILED;
                                return std::unexpected(ParseError{
                                    .code            = ParseError::CODE::GENERIC_ERROR,
                                    .expected_status = Status::URI_TOO_LONG,
                                    .message         = "URI too long",
                                });
                            }
                            continue;
                        } else if (c == ']') {
                            ++begin;
                            ++ctx.current_state_size;
                            state = STATE::AUTHORITY_FORM_AFTER_HOST;
                            break;
                        } else {
                            state = STATE::FAILED;
                            return std::unexpected(ParseError{
                                .code            = ParseError::CODE::GENERIC_ERROR,
                                .expected_status = Status::BAD_REQUEST,
                                .message         = "Invalid IPv6 host character",
                            });
                        }
                    }
                    break;
                }
                case STATE::AUTHORITY_FORM_AFTER_HOST: {
                    if (begin == end) break;
                    if (*begin != ':') {
                        state = STATE::FAILED;
                        return std::unexpected(ParseError{
                            .code            = ParseError::CODE::GENERIC_ERROR,
                            .expected_status = Status::BAD_REQUEST,
                            .message         = "Authority-form requires ':port'",
                        });
                    }
                    ++begin;
                    ++ctx.current_state_size;
                    state = STATE::AUTHORITY_FORM_PORT;
                    break;
                }
                case STATE::AUTHORITY_FORM_PORT: {
                    auto &port = request.metadata.uri.authority.port;
                    bool any_digit = false;
                    while (begin != end) {
                        if (isDigit(*begin)) {
                            std::uint32_t p = port * 10u + static_cast<std::uint32_t>(*begin - '0');
                            if (p > 65535u) {
                                state = STATE::FAILED;
                                return std::unexpected(ParseError{
                                    .code            = ParseError::CODE::GENERIC_ERROR,
                                    .expected_status = Status::BAD_REQUEST,
                                    .message         = "Port out of range",
                                });
                            }
                            port = static_cast<std::uint16_t>(p);
                            ++begin;
                            ++ctx.current_state_size;
                            any_digit = true;
                            if (ctx.current_state_size >= usub::unet::http::max_uri_size) {
                                state = STATE::FAILED;
                                return std::unexpected(ParseError{
                                    .code            = ParseError::CODE::GENERIC_ERROR,
                                    .expected_status = Status::URI_TOO_LONG,
                                    .message         = "URI too long",
                                });
                            }
                            continue;
                        } else if (*begin == ' ') {
                            if (!any_digit && port == 0) {
                                state = STATE::FAILED;
                                return std::unexpected(ParseError{
                                    .code            = ParseError::CODE::GENERIC_ERROR,
                                    .expected_status = Status::BAD_REQUEST,
                                    .message         = "Authority-form requires port digits",
                                });
                            }
                            ++begin;
                            ctx.current_state_size = 0;
                            state = STATE::VERSION;
                            break;
                        } else {
                            state = STATE::FAILED;
                            return std::unexpected(ParseError{
                                .code            = ParseError::CODE::GENERIC_ERROR,
                                .expected_status = Status::BAD_REQUEST,
                                .message         = "Invalid port character",
                            });
                        }
                    }
                    break;
                }
                case STATE::ASTERISK_FORM: {
                    if (begin == end) break;
                    if (ctx.current_state_size == 0) {
                        if (*begin != '*') {
                            state = STATE::FAILED;
                            return std::unexpected(ParseError{
                                .code            = ParseError::CODE::GENERIC_ERROR,
                                .expected_status = Status::BAD_REQUEST,
                                .message         = "Asterisk form expected '*'",
                            });
                        }
                        request.metadata.uri.path = "*";
                        ++begin;
                        ctx.current_state_size = 1;
                        if (begin == end) break;
                    }
                    if (*begin == ' ') {
                        state = STATE::VERSION;
                        ++begin;
                        ctx.current_state_size = 0;
                    } else {
                        state = STATE::FAILED;
                        return std::unexpected(ParseError{
                            .code            = ParseError::CODE::GENERIC_ERROR,
                            .expected_status = Status::BAD_REQUEST,
                            .message         = "Unknown Asterisk form char",
                        });
                    }
                    break;
                }
                case STATE::VERSION: {
                    auto &version_buf = ctx.kv_buffer.first;
                    while (begin != end) {
                        if (isVersion(*begin)) {
                            version_buf.push_back(static_cast<char>(*begin));
                            ++ctx.current_state_size;
                            ++begin;
                        } else if (*begin == '\r') {
                            state = STATE::REQUEST_LINE_CRLF;
                            ++begin;
                            break;
                        } else {
                            state = STATE::FAILED;
                            return std::unexpected(ParseError{
                                .code            = ParseError::CODE::GENERIC_ERROR,
                                .expected_status = Status::BAD_REQUEST,
                                .message         = "Wrong Version",
                            });
                        }
                        if (ctx.current_state_size > 8) {
                            state = STATE::FAILED;
                            return std::unexpected(ParseError{
                                .code            = ParseError::CODE::GENERIC_ERROR,
                                .expected_status = Status::BAD_REQUEST,
                                .message         = "Version too large",
                            });
                        }
                    }
                    [[fallthrough]];
                }
                case STATE::REQUEST_LINE_CRLF: {
                    if (begin == end) [[unlikely]] { return {}; }
                    auto &version_buf = ctx.kv_buffer.first;
                    if (*begin != '\n') {
                        state = STATE::FAILED;
                        return std::unexpected(ParseError{
                            .code            = ParseError::CODE::GENERIC_ERROR,
                            .expected_status = Status::BAD_REQUEST,
                            .message         = "Missing LF",
                        });
                    }
                    ++begin;
                    if (version_buf == "HTTP/1.1") {
                        request.metadata.version = VERSION::HTTP_1_1;
                    } else if (version_buf == "HTTP/1.0") {
                        request.metadata.version = VERSION::HTTP_1_0;
                    } else {
                        state = STATE::FAILED;
                        return std::unexpected(ParseError{
                            .code            = ParseError::CODE::GENERIC_ERROR,
                            .expected_status = Status::BAD_REQUEST,
                            .message         = "Unknown version",
                        });
                    }
                    ctx.kv_buffer.first.clear();
                    ctx.current_state_size = 0;
                    state = STATE::HEADER_KEY;
                    [[fallthrough]];
                }
                case STATE::HEADER_KEY: {
                    auto &key = ctx.kv_buffer.first;
                    while (begin != end) {
                        if (isTchar(*begin)) [[likely]] {
                            key.push_back(static_cast<char>(*begin));
                            ++begin;
                            ++ctx.headers_size;
                            continue;
                        } else if (*begin == ':') {
                            state = STATE::HEADER_VALUE;
                            ++ctx.headers_size;
                            ++begin;
                            break;
                        }
                        state = STATE::FAILED;
                        return std::unexpected(ParseError{
                            .code            = ParseError::CODE::GENERIC_ERROR,
                            .expected_status = Status::BAD_REQUEST,
                            .message         = "Invalid character in header name",
                        });
                    }
                    // Since our reads are limited by 16 kb, there should be no case where not checking this
                    // after every append can cause problems
                    if (ctx.headers_size > usub::unet::http::max_headers_size) {
                        state = STATE::FAILED;
                        return std::unexpected(ParseError{
                            .code            = ParseError::CODE::GENERIC_ERROR,
                            .expected_status = Status::REQUEST_HEADER_FIELDS_TOO_LARGE,
                            .message         = "Headers too large",
                        });
                    }
                    break;
                }
                case STATE::HEADER_VALUE: {
                    auto &value = ctx.kv_buffer.second;
                    while (begin != end) {
                        if (isVcharOrObs(*begin)) {
                            value.push_back(static_cast<char>(*begin));
                            ++begin;
                            ++ctx.headers_size;
                            continue;
                        } else if (*begin == '\r') {
                            ++begin;
                            ++ctx.headers_size;
                            state = STATE::HEADER_CR;
                            break;
                        }

                        state = STATE::FAILED;
                        return std::unexpected(ParseError{
                            .code            = ParseError::CODE::GENERIC_ERROR,
                            .expected_status = Status::BAD_REQUEST,
                            .message         = "Invalid header value",
                        });
                    }
                    // Since our reads are limited by 16 kb, there should be no case where not checking this
                    // after every append can cause problems
                    if (ctx.headers_size > usub::unet::http::max_headers_size) {
                        state = STATE::FAILED;
                        return std::unexpected(ParseError{
                            .code            = ParseError::CODE::GENERIC_ERROR,
                            .expected_status = Status::REQUEST_HEADER_FIELDS_TOO_LARGE,
                            .message         = "Headers too large",
                        });
                    }
                    break;
                }
                case STATE::HEADER_CR: {
                    // REMINDER: No need to check for begin == end, we break in prev case
                    if (*begin == '\n') {
                        ++begin;
                        ++ctx.headers_size;
                        state = STATE::HEADER_LF;
                    } else {
                        state = STATE::FAILED;
                        return std::unexpected(ParseError{
                            .code            = ParseError::CODE::GENERIC_ERROR,
                            .expected_status = Status::BAD_REQUEST,
                            .message         = "Header Missing LF",
                        });
                    }
                    [[fallthrough]];
                }
                case STATE::HEADER_LF: {
                    auto &[key, value] = ctx.kv_buffer;
                    request.headers.addHeader(std::move(key), std::move(value));
                    if (begin == end) { return {}; }
                    if (isTchar(*begin)) {
                        state = STATE::HEADER_KEY;
                    } else if (*begin == '\r') {
                        ++begin;
                        ++ctx.headers_size;
                        state = STATE::HEADERS_CRLF;
                    } else {
                        state = STATE::FAILED;
                        return std::unexpected(ParseError{
                            .code            = ParseError::CODE::GENERIC_ERROR,
                            .expected_status = Status::BAD_REQUEST,
                            .message         = "Header Missing CR/unknown char",
                        });
                    }
                    break;
                }
                case STATE::HEADERS_CRLF: {
                    // REMINDER: No need to check for begin == end, we break in prev case
                    if (*begin == '\n') {
                        ++begin;
                        ++ctx.headers_size;
                        if (ctx.headers_size > usub::unet::http::max_headers_size) {
                            state = STATE::FAILED;
                            return std::unexpected(ParseError{
                                .code            = ParseError::CODE::GENERIC_ERROR,
                                .expected_status = Status::REQUEST_HEADER_FIELDS_TOO_LARGE,
                                .message         = "Headers too large",
                            });
                        }
                        state = STATE::HEADERS_VALIDATION;
                        // return {};
                    } else {
                        state = STATE::FAILED;
                        return std::unexpected(ParseError{
                            .code            = ParseError::CODE::GENERIC_ERROR,
                            .expected_status = Status::BAD_REQUEST,
                            .message         = "Header Missing CR/unknown char",
                        });
                    }
                    [[fallthrough]];
                }
                case STATE::HEADERS_VALIDATION: {
                    ctx.current_state_size = 0;

                    std::size_t content_length_value = 0;
                    bool content_length_seen = false;
                    std::optional<std::string_view> host_value;
                    bool has_transfer_encoding = false;
                    bool has_chunked = false;
                    bool has_other_encoding = false;

                    for (const auto &header: request.headers.all()) {
                        const std::string_view key = header.key;

                        if (key == "host") {
                            if (!host_value.has_value()) { host_value = header.value; }
                            continue;
                        }

                        if (key == "content-length") {
                            std::string_view value = header.value;
                            while (!value.empty()) {
                                const std::size_t comma = value.find(',');
                                std::string_view token =
                                        (comma == std::string_view::npos) ? value : value.substr(0, comma);
                                token = trim_ows(token);
                                std::size_t parsed = 0;
                                if (!parse_uint(token, parsed)) {
                                    state = STATE::FAILED;
                                    return std::unexpected(ParseError{
                                        .code            = ParseError::CODE::GENERIC_ERROR,
                                        .expected_status = Status::BAD_REQUEST,
                                        .message         = "Invalid Content-Length",
                                    });
                                }
                                if (!content_length_seen) {
                                    content_length_value = parsed;
                                    content_length_seen = true;
                                } else if (parsed != content_length_value) {
                                    state = STATE::FAILED;
                                    return std::unexpected(ParseError{
                                        .code            = ParseError::CODE::GENERIC_ERROR,
                                        .expected_status = Status::BAD_REQUEST,
                                        .message         = "Conflicting Content-Length",
                                    });
                                }
                                if (comma == std::string_view::npos) break;
                                value.remove_prefix(comma + 1);
                            }
                            continue;
                        }

                        if (key != "transfer-encoding") { continue; }

                        has_transfer_encoding = true;
                        std::string_view value = header.value;
                        while (!value.empty()) {
                            const std::size_t comma = value.find(',');
                            std::string_view token = (comma == std::string_view::npos) ? value : value.substr(0, comma);
                            token = trim_ows(token);
                            if (token.empty()) {
                                state = STATE::FAILED;
                                return std::unexpected(ParseError{
                                    .code            = ParseError::CODE::GENERIC_ERROR,
                                    .expected_status = Status::BAD_REQUEST,
                                    .message         = "Invalid Transfer-Encoding",
                                });
                            }
                            if (is_chunked_token(token)) {
                                has_chunked = true;
                            } else {
                                has_other_encoding = true;
                            }
                            if (comma == std::string_view::npos) break;
                            value.remove_prefix(comma + 1);
                        }
                    }

                    if (!host_value.has_value()) {
                        state = STATE::FAILED;
                        return std::unexpected(ParseError{
                            .code            = ParseError::CODE::GENERIC_ERROR,
                            .expected_status = Status::BAD_REQUEST,
                            .message         = "Missing Host header",
                        });
                    }
                    request.metadata.authority = *host_value;

                    if (has_transfer_encoding) {
                        if (request.metadata.version != VERSION::HTTP_1_1) {
                            state = STATE::FAILED;
                            return std::unexpected(ParseError{
                                .code            = ParseError::CODE::GENERIC_ERROR,
                                .expected_status = Status::BAD_REQUEST,
                                .message         = "Transfer-Encoding not allowed",
                            });
                        }
                        if (!has_chunked || has_other_encoding) {
                            state = STATE::FAILED;
                            return std::unexpected(ParseError{
                                .code            = ParseError::CODE::GENERIC_ERROR,
                                .expected_status = Status::BAD_REQUEST,
                                .message         = "Unsupported Transfer-Encoding",
                            });
                        }
                    }

                    if (has_chunked && content_length_seen) {
                        state = STATE::FAILED;
                        return std::unexpected(ParseError{
                            .code            = ParseError::CODE::GENERIC_ERROR,
                            .expected_status = Status::BAD_REQUEST,
                            .message         = "Both Transfer-Encoding and Content-Length present",
                        });
                    }

                    if (has_chunked) {
                        ctx.current_state_size = 0;
                        ctx.body_type = BODY_TYPE::CHUNKED;
                    } else if (content_length_seen && content_length_value != 0) {
                        ctx.body_read_size = content_length_value;
                        ctx.body_type = BODY_TYPE::CONTENT_LENGTH;
                    } else {
                        ctx.body_type = BODY_TYPE::NONE;
                    }
                    state = STATE::HEADERS_DONE;
                    return {};
                }
                case STATE::DATA_CONTENT_LENGTH: {
                    auto &content_length = ctx.body_read_size;
                    std::size_t already = static_cast<std::size_t>(ctx.current_state_size);

                    const std::size_t remaining = content_length - already;
                    const std::size_t available = static_cast<std::size_t>(end - begin);
                    const std::size_t take = (available < remaining) ? available : remaining;

                    if (already >= content_length) break;

                    pending_body_.append(&*begin, take);

                    begin += take;
                    ctx.current_state_size += take;
                    ctx.body_bytes_read += take;

                    if (ctx.current_state_size == content_length) {
                        state = STATE::COMPLETE;
                        return {};
                    } else if (ctx.current_state_size > content_length) {
                        state = STATE::FAILED;
                        return std::unexpected(ParseError{
                            .code            = ParseError::CODE::GENERIC_ERROR,
                            .expected_status = Status::PAYLOAD_TOO_LARGE,
                            .message         = "Body size too big",
                        });
                    }

                    break;
                }
                case STATE::DATA_CHUNKED_SIZE: {
                    while (begin != end) {
                        if (isHexDigit(static_cast<unsigned char>(*begin))) {
                            ctx.kv_buffer.first.push_back(*begin);
                            ++begin;
                            ++ctx.body_bytes_read;
                        } else if (*begin == '\r') {
                            ++begin;
                            ++ctx.body_bytes_read;
                            state = STATE::DATA_CHUNKED_SIZE_CRLF;
                            break;
                        } else {
                            // We dont support chunked extensions, that thing is obsoleted and has 0 use cases i can find
                            // If the need arises, we will, it's not that hard to implement now, but for now, to hell with it

                            // For now, I just refuse to do so
                            state = STATE::FAILED;
                            return std::unexpected(ParseError{
                                .code            = ParseError::CODE::GENERIC_ERROR,
                                .expected_status = Status::BAD_REQUEST,
                                .message         = "Unknown symbol in chunked size",
                            });
                        }
                    }
                    break;
                }
                case STATE::DATA_CHUNKED_SIZE_CRLF: {
                    if (*begin == '\n') {
                        std::size_t chunk_size = 0;
                        if (!parse_hex_size(ctx.kv_buffer.first, chunk_size)) {
                            state = STATE::FAILED;
                            return std::unexpected(ParseError{
                                .code            = ParseError::CODE::GENERIC_ERROR,
                                .expected_status = Status::BAD_REQUEST,
                                .message         = "Invalid chunk size",
                            });
                        }
                        ctx.body_read_size = chunk_size;
                        ctx.kv_buffer.first.clear();
                        ctx.current_state_size = 0;
                        ++begin;
                        ++ctx.body_bytes_read;
                        state = STATE::DATA_CHUNKED_DATA;
                        if (ctx.body_read_size == 0) { state = STATE::DATA_CHUNKED_LAST_CR; }
                    } else {
                        state = STATE::FAILED;
                        return std::unexpected(ParseError{
                            .code            = ParseError::CODE::GENERIC_ERROR,
                            .expected_status = Status::BAD_REQUEST,
                            .message         = "Missing LF in chunked size",
                        });
                    }
                    break;
                }
                case STATE::DATA_CHUNKED_DATA: {
                    const std::size_t remaining = static_cast<std::size_t>(ctx.body_read_size - ctx.current_state_size);
                    if (remaining == 0) {
                        state = STATE::DATA_CHUNKED_DATA_CR;
                        break;
                    }

                    const std::size_t available = static_cast<std::size_t>(end - begin);
                    const std::size_t take = (available < remaining) ? available : remaining;

                    pending_body_.append(&*begin, take);

                    begin += take;
                    ctx.current_state_size += take;
                    ctx.body_bytes_read += take;

                    if (ctx.current_state_size == ctx.body_read_size) {
                        state = STATE::DATA_CHUNKED_DATA_CR;
                        break;
                    }

                    break;
                }
                case STATE::DATA_CHUNKED_DATA_CR: {
                    if (*begin != '\r') {
                        state = STATE::FAILED;
                        return std::unexpected(ParseError{
                            .code            = ParseError::CODE::GENERIC_ERROR,
                            .expected_status = Status::BAD_REQUEST,
                            .message         = "Missing CR after chunk data",
                        });
                    }
                    ++begin;
                    ++ctx.body_bytes_read;
                    state = STATE::DATA_CHUNKED_DATA_LF;
                }
                case STATE::DATA_CHUNKED_DATA_LF: {
                    if (begin == end) break;
                    if (*begin != '\n') {
                        state = STATE::FAILED;
                        return std::unexpected(ParseError{
                            .code            = ParseError::CODE::GENERIC_ERROR,
                            .expected_status = Status::BAD_REQUEST,
                            .message         = "Missing LF after chunk data",
                        });
                    }
                    ++begin;
                    ++ctx.body_bytes_read;

                    state = STATE::DATA_CHUNK_DONE;
                    return {};
                }
                case STATE::DATA_CHUNK_DONE: {
                    ctx.current_state_size = 0;
                    ctx.body_read_size = 0;
                    ctx.kv_buffer.first.clear();
                    state = STATE::DATA_CHUNKED_SIZE;
                    break;
                }
                case STATE::DATA_CHUNKED_LAST_CR: {
                    if (*begin == '\r') {
                        ++begin;
                        ++ctx.body_bytes_read;
                        state = STATE::DATA_CHUNKED_LAST_LF;
                    } else {
                        state = STATE::FAILED;
                        return std::unexpected(ParseError{
                            .code            = ParseError::CODE::GENERIC_ERROR,
                            .expected_status = Status::BAD_REQUEST,
                            .message         = "Missing CR DATA_CHUNKED_LAST_CR",
                        });
                    }
                    [[fallthrough]];
                }
                case STATE::DATA_CHUNKED_LAST_LF: {
                    if (begin == end) { return {}; }
                    if (*begin == '\n') {
                        ++begin;
                        ++ctx.body_bytes_read;
                        state = STATE::DATA_DONE;
                    } else {
                        state = STATE::FAILED;
                        return std::unexpected(ParseError{
                            .code            = ParseError::CODE::GENERIC_ERROR,
                            .expected_status = Status::BAD_REQUEST,
                            .message         = "Missing LF DATA_CHUNKED_LAST_LF",
                        });
                    }
                    [[fallthrough]];
                }
                case STATE::DATA_DONE:
                    if (begin != end) {
                        state = STATE::FAILED;
                        return std::unexpected(ParseError{
                            .code            = ParseError::CODE::GENERIC_ERROR,
                            .expected_status = Status::BAD_REQUEST,
                            .message         = "Trailers unsupported yet",
                        });
                    }
                    state = STATE::COMPLETE;
                    return {};
                case STATE::COMPLETE:
                    return {};
                case STATE::FAILED:
                    begin = end;
                    state = STATE::FAILED;
                    return std::unexpected(ParseError{
                        .code            = ParseError::CODE::GENERIC_ERROR,
                        .expected_status = Status::BAD_REQUEST,
                        .message         = "Parser in failed state",
                    });
                default:
                    state = STATE::FAILED;
                    return std::unexpected(ParseError{
                        .code            = ParseError::CODE::GENERIC_ERROR,
                        .expected_status = Status::BAD_REQUEST,
                        .message         = "Invalid parser state",
                    });
            }
        }
        return {};
    }

    RequestParser::ParserContext &RequestParser::getContext() { return this->context_; }
}// namespace usub::unet::http::v1
