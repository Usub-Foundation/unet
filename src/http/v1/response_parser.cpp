#include "unet/http/v1/response_parser.hpp"


namespace usub::unet::http::v1 {

    namespace {
        constexpr std::array<std::uint8_t, 256> build_tchar_table() {
            std::array<std::uint8_t, 256> table{};
            for (char c = 'A'; c <= 'Z'; ++c) table[static_cast<unsigned char>(c)] = 1;
            for (char c = 'a'; c <= 'z'; ++c) table[static_cast<unsigned char>(c)] = 1;
            for (char c = '0'; c <= '9'; ++c) table[static_cast<unsigned char>(c)] = 1;
            for (char c: {'!', '#', '$', '%', '&', '\'', '*', '+', '-', '.', '^', '_', '`', '|', '~'}) {
                table[static_cast<unsigned char>(c)] = 1;
            }
            return table;
        }

        constexpr std::array<std::uint8_t, 256> build_vchar_obs_table() {
            std::array<std::uint8_t, 256> table{};
            for (char c = '!'; c <= '~'; ++c) table[static_cast<unsigned char>(c)] = 1;
            for (unsigned char c = 128; c <= 255 && c >= 128; ++c) table[c] = 1;

            // Should be supported?
            table[' '] = 1;
            table['\t'] = 1;
            return table;
        }

        constexpr std::array<std::uint8_t, 256> build_scheme_table() {
            std::array<std::uint8_t, 256> table{};
            for (char c = 'A'; c <= 'Z'; ++c) table[static_cast<unsigned char>(c)] = 1;
            for (char c = 'a'; c <= 'z'; ++c) table[static_cast<unsigned char>(c)] = 1;
            for (char c = '0'; c <= '9'; ++c) table[static_cast<unsigned char>(c)] = 1;
            for (char c: {'+', '-', '.'}) { table[static_cast<unsigned char>(c)] = 1; }
            return table;
        }

        constexpr std::array<std::uint8_t, 256> build_path_table() {
            std::array<std::uint8_t, 256> table{};
            for (char c = 'A'; c <= 'Z'; ++c) table[static_cast<unsigned char>(c)] = 1;
            for (char c = 'a'; c <= 'z'; ++c) table[static_cast<unsigned char>(c)] = 1;
            for (char c = '0'; c <= '9'; ++c) table[static_cast<unsigned char>(c)] = 1;
            for (char c: {'-', '.', '_', '~'}) { table[static_cast<unsigned char>(c)] = 1; }
            for (char c: {'!', '$', '&', '\'', '(', ')', '*', '+', ',', ';', '='}) {
                table[static_cast<unsigned char>(c)] = 1;
            }
            for (char c: {':', '@', '/'}) { table[static_cast<unsigned char>(c)] = 1; }
            return table;
        }

        constexpr std::array<std::uint8_t, 256> build_query_table() {
            std::array<std::uint8_t, 256> table{};
            for (char c = 'A'; c <= 'Z'; ++c) table[static_cast<unsigned char>(c)] = 1;
            for (char c = 'a'; c <= 'z'; ++c) table[static_cast<unsigned char>(c)] = 1;
            for (char c = '0'; c <= '9'; ++c) table[static_cast<unsigned char>(c)] = 1;
            for (char c: {'-', '.', '_', '~'}) { table[static_cast<unsigned char>(c)] = 1; }
            for (char c: {'!', '$', '&', '\'', '(', ')', '*', '+', ',', ';', '='}) {
                table[static_cast<unsigned char>(c)] = 1;
            }
            for (char c: {':', '@', '/', '?', '%'}) { table[static_cast<unsigned char>(c)] = 1; }
            return table;
        }

        constexpr std::array<std::uint8_t, 256> build_host_table() {
            std::array<std::uint8_t, 256> table{};
            for (char c = 'A'; c <= 'Z'; ++c) table[static_cast<unsigned char>(c)] = 1;
            for (char c = 'a'; c <= 'z'; ++c) table[static_cast<unsigned char>(c)] = 1;
            for (char c = '0'; c <= '9'; ++c) table[static_cast<unsigned char>(c)] = 1;
            for (char c: {'-', '.', '_', '~'}) { table[static_cast<unsigned char>(c)] = 1; }
            for (char c: {'!', '$', '&', '\'', '(', ')', '*', '+', ',', ';', '=', '%'}) {
                table[static_cast<unsigned char>(c)] = 1;
            }
            return table;
        }

        constexpr std::array<std::uint8_t, 256> build_version_table() {
            std::array<std::uint8_t, 256> table{};
            table['H'] = 1;
            table['T'] = 1;
            table['P'] = 1;
            table['/'] = 1;
            table['1'] = 1;
            table['.'] = 1;
            table['0'] = 1;
            return table;
        }

        constexpr std::array<std::uint8_t, 256> tchar_table = build_tchar_table();
        constexpr std::array<std::uint8_t, 256> vchar_obs_table = build_vchar_obs_table();
        constexpr std::array<std::uint8_t, 256> scheme_table = build_scheme_table();
        constexpr std::array<std::uint8_t, 256> path_table = build_path_table();
        constexpr std::array<std::uint8_t, 256> query_table = build_query_table();
        constexpr std::array<std::uint8_t, 256> host_table = build_host_table();
        constexpr std::array<std::uint8_t, 256> version_table = build_version_table();

        inline bool is_version(unsigned char c) { return version_table[c] != 0; }

        inline bool is_alpha(unsigned char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

        inline bool is_tchar(unsigned char c) { return tchar_table[c] != 0; }

        inline bool is_vchar_or_obs(unsigned char c) { return vchar_obs_table[c] != 0; }

        inline bool is_scheme_char(unsigned char c) { return scheme_table[c] != 0; }

        inline bool is_path_char(unsigned char c) { return path_table[c] != 0; }

        inline bool is_query_char(unsigned char c) { return query_table[c] != 0; }

        inline bool is_host_char(unsigned char c) { return host_table[c] != 0; }

        inline char ascii_lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c; }

        inline bool is_hex_digit(unsigned char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        }

        inline std::uint8_t hex_value(unsigned char c) {
            if (c >= '0' && c <= '9') return static_cast<std::uint8_t>(c - '0');
            if (c >= 'a' && c <= 'f') return static_cast<std::uint8_t>(10 + (c - 'a'));
            return static_cast<std::uint8_t>(10 + (c - 'A'));
        }

        inline std::string_view trim_ows(const std::string &value) {
            std::size_t start = 0;
            std::size_t end = value.size();
            while (start < end && (value[start] == ' ' || value[start] == '\t')) { ++start; }
            while (end > start && (value[end - 1] == ' ' || value[end - 1] == '\t')) { --end; }
            return std::string_view(value.data() + start, end - start);
        }

        inline bool contains_chunked_token(std::string_view value) {
            constexpr std::string_view token = "chunked";
            std::size_t i = 0;
            while (i < value.size()) {
                while (i < value.size() && (value[i] == ' ' || value[i] == '\t' || value[i] == ',')) { ++i; }
                if (i + token.size() > value.size()) break;
                bool match = true;
                for (std::size_t j = 0; j < token.size(); ++j) {
                    char c = value[i + j];
                    if (ascii_lower(c) != token[j]) {
                        match = false;
                        break;
                    }
                }
                if (match) return true;
                while (i < value.size() && value[i] != ',') { ++i; }
            }
            return false;
        }
        bool parse_uint(std::string_view value, std::size_t &out) {
            if (value.empty()) return false;

            const char *begin = value.data();
            const char *end = begin + value.size();

            auto [ptr, ec] = std::from_chars(begin, end, out);

            return ec == std::errc() && ptr == end;
        }

        inline bool parse_hex_size(std::string_view s, std::size_t &out) {
            if (s.empty()) return false;
            std::size_t v = 0;
            for (unsigned char ch: s) {
                if (!is_hex_digit(ch)) return false;
                const std::size_t digit = hex_value(ch);
                if (v > (std::numeric_limits<std::size_t>::max() - digit) / 16) return false;
                v = v * 16 + digit;
            }
            out = v;
            return true;
        }

    }// namespace
    std::expected<Response, ParseError> ResponseParser::parse(const std::string_view raw_response) {
        ResponseParser parser;
        Response response;
        auto begin = raw_response.begin();
        auto end = raw_response.end();

        for (;;) {
            auto result = parser.parse(response, begin, end);
            if (!result) { return std::unexpected(result.error()); }
            if (parser.context_.state == STATE::COMPLETE) { return response; }
            if (begin == end) {
                ParseError err{ParseError::CODE::GENERIC_ERROR, STATUS_CODE::BAD_REQUEST, "Incomplete response", {}};
                return std::unexpected(err);
            }
        }
    }
    std::expected<void, ParseError> ResponseParser::parse(Response &response, std::string_view::const_iterator &begin,
                                                          const std::string_view::const_iterator end) {
        auto &ctx = this->context_;
        auto &state = ctx.state;

        using Status = usub::unet::http::STATUS_CODE;

        auto fail = [&](Status status, std::string_view message) -> std::expected<void, ParseError> {
            ParseError err{};
            err.code = ParseError::CODE::GENERIC_ERROR;
            err.expected_status = status;
            err.message = std::string(message);
            state = STATE::FAILED;
            return std::unexpected(err);
        };


        while (begin != end) {
            switch (state) {
                case STATE::STATUS_VERSION: {
                    auto &ver_buf = ctx.kv_buffer.first;

                    if (ver_buf.empty() && ctx.current_state_size == 0) {
                        ctx.headers_size = 0;
                        ctx.body_bytes_read = 0;
                        ctx.kv_buffer = {};
                    }
                    while (begin != end) {
                        const char ch = *begin;

                        if (ver_buf.empty() && ch >= '0' && ch <= '9') {

                            ctx.current_state_size = 0;
                            state = STATE::STATUS_CODE;
                            break;
                        }

                        if (ch == ' ') {
                            if (ver_buf.empty()) {
                                return fail(Status::BAD_REQUEST, "Empty HTTP version in status line");
                            }

                            if (ver_buf == "HTTP/1.1") {
                                response.metadata.version = VERSION::HTTP_1_1;
                            } else if (ver_buf == "HTTP/1.0") {
                                response.metadata.version = VERSION::HTTP_1_0;
                            } else {
                                return fail(Status::BAD_REQUEST, "Unknown HTTP version in status line");
                            }

                            ++begin;
                            ver_buf.clear();
                            ctx.current_state_size = 0;
                            state = STATE::STATUS_CODE;
                            break;
                        }

                        if (ch == '\r' || ch == '\n') {
                            return fail(Status::BAD_REQUEST, "Malformed status line (version not followed by SP)");
                        }

                        if (!is_version(static_cast<unsigned char>(ch))) {
                            return fail(Status::BAD_REQUEST, "Invalid character in HTTP version");
                        }

                        ver_buf.push_back(ch);
                        ++begin;

                        if (ver_buf.size() > 16) { return fail(Status::BAD_REQUEST, "HTTP version too large"); }
                    }
                    break;
                }

                case STATE::STATUS_CODE: {

                    if (ctx.current_state_size == 0) { response.metadata.status_code = 0; }

                    while (begin != end) {
                        const char ch = *begin;

                        if (ch >= '0' && ch <= '9') {
                            if (ctx.current_state_size >= 3) {//legacy response without HTTP-version token
                                return fail(Status::BAD_REQUEST, "Status code too long");
                            }

                            auto &code = response.metadata.status_code;// 0..999
                            code = static_cast<std::uint16_t>(code * 10u + static_cast<unsigned>(ch - '0'));

                            if (code >= 1000u) { return fail(Status::BAD_REQUEST, "Status code out of range"); }

                            ++ctx.current_state_size;
                            ++begin;
                            continue;
                        }

                        if (ch == ' ') {
                            if (ctx.current_state_size != 3) {
                                return fail(Status::BAD_REQUEST, "Status code must be 3 digits");
                            }

                            ++begin;
                            ctx.current_state_size = 0;
                            state = STATE::STATUS_REASON;
                            break;
                        }

                        return fail(Status::BAD_REQUEST, "Invalid status code (expected digit or SP)");
                    }

                    break;
                }

                case STATE::STATUS_REASON: {
                    auto &reason = ctx.kv_buffer.second;

                    while (begin != end) {
                        const char ch = *begin;

                        if (ch == '\r') {
                            ++begin;// consume CR
                            state = STATE::STATUS_LINE_CRLF;
                            break;
                        }

                        if (!is_vchar_or_obs(static_cast<unsigned char>(ch))) {
                            return fail(Status::BAD_REQUEST, "Invalid character in reason phrase");
                        }

                        reason.push_back(ch);
                        ++begin;

                        if (reason.size() > 1024) { return fail(Status::BAD_REQUEST, "Reason phrase too large"); }
                    }
                    break;
                }

                case STATE::STATUS_LINE_CRLF: {
                    if (begin == end) { return {}; }

                    if (*begin != '\n') { return fail(Status::BAD_REQUEST, "Missing LF after CR in status line"); }
                    ++begin;

                    response.metadata.status_message = std::move(ctx.kv_buffer.second);
                    ctx.kv_buffer.second.clear();

                    ctx.current_state_size = 0;
                    state = STATE::HEADER_KEY;
                    break;
                }

                case STATE::HEADER_KEY: {
                    auto &key = ctx.kv_buffer.first;
                    while (begin != end) {
                        if (is_tchar(*begin)) [[likely]] {
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
                        return fail(Status::BAD_REQUEST, "Invalid character in header name");
                    }
                    // Since our reads are limited by 16 kb, there should be no case where not checking this
                    // after every append can cause problems
                    if (ctx.headers_size > usub::unet::http::max_headers_size) {
                        return fail(Status::REQUEST_HEADER_FIELDS_TOO_LARGE, "Headers too large");
                    }
                    break;
                }
                case STATE::HEADER_VALUE: {
                    auto &value = ctx.kv_buffer.second;
                    while (begin != end) {
                        if (is_vchar_or_obs(*begin)) {
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

                        return fail(Status::BAD_REQUEST, "Invalid header value");
                    }
                    // Since our reads are limited by 16 kb, there should be no case where not checking this
                    // after every append can cause problems
                    if (ctx.headers_size > usub::unet::http::max_headers_size) {
                        return fail(Status::REQUEST_HEADER_FIELDS_TOO_LARGE, "Headers too large");
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
                        return fail(Status::BAD_REQUEST, "Header Missing LF");
                    }
                    [[fallthrough]];
                }
                case STATE::HEADER_LF: {
                    auto &[key, value] = ctx.kv_buffer;
                    response.headers.addHeader(std::move(key), std::move(value));
                    if (begin == end) { return {}; }
                    if (is_tchar(*begin)) {
                        state = STATE::HEADER_KEY;
                    } else if (*begin == '\r') {
                        ++begin;
                        ++ctx.headers_size;
                        state = STATE::HEADERS_CRLF;
                    } else {
                        return fail(Status::BAD_REQUEST, "Header Missing CR/unknown char");
                    }
                    break;
                }
                case STATE::HEADERS_CRLF: {
                    // REMINDER: No need to check for begin == end, we break in prev case
                    if (*begin == '\n') {
                        ++begin;
                        ++ctx.headers_size;
                        if (ctx.headers_size > usub::unet::http::max_headers_size) {
                            return fail(Status::REQUEST_HEADER_FIELDS_TOO_LARGE, "Headers too large");
                        }
                        state = STATE::HEADERS_VALIDATION;
                        // return {};
                    } else {
                        return fail(Status::BAD_REQUEST, "Header Missing CR/unknown char");
                    }
                    [[fallthrough]];
                }

                case STATE::HEADERS_VALIDATION: {
                    ctx.current_state_size = 0;

                    const auto content_length_headers = response.headers.all("content-length");
                    const auto transfer_encoding_headers = response.headers.all("transfer-encoding");
                    const bool has_transfer_encoding = !transfer_encoding_headers.empty();

                    std::size_t content_length_value = 0;
                    bool content_length_seen = false;

                    auto trim_view = [](std::string_view value) -> std::string_view {
                        std::size_t start = 0;
                        std::size_t end = value.size();
                        while (start < end && (value[start] == ' ' || value[start] == '\t')) { ++start; }
                        while (end > start && (value[end - 1] == ' ' || value[end - 1] == '\t')) { --end; }
                        return value.substr(start, end - start);
                    };

                    auto parse_uint = [](std::string_view value, std::size_t &out) -> bool {
                        if (value.empty()) return false;
                        std::size_t result = 0;
                        for (char c: value) {
                            if (c < '0' || c > '9') return false;
                            const std::size_t digit = static_cast<std::size_t>(c - '0');
                            if (result > (std::numeric_limits<std::size_t>::max() - digit) / 10) return false;
                            result = result * 10 + digit;
                        }
                        out = result;
                        return true;
                    };


                    for (const auto &header: content_length_headers) {
                        std::string_view value = header.value;
                        while (!value.empty()) {
                            const std::size_t comma = value.find(',');
                            std::string_view token = (comma == std::string_view::npos) ? value : value.substr(0, comma);
                            token = trim_view(token);

                            std::size_t parsed = 0;
                            if (!parse_uint(token, parsed)) {
                                return fail(Status::BAD_REQUEST, "Invalid Content-Length");
                            }

                            if (!content_length_seen) {
                                content_length_value = parsed;
                                content_length_seen = true;
                            } else if (parsed != content_length_value) {
                                return fail(Status::BAD_REQUEST, "Conflicting Content-Length");
                            }

                            if (comma == std::string_view::npos) break;
                            value.remove_prefix(comma + 1);
                        }
                    }


                    bool has_chunked = false;
                    bool has_other_encoding = false;

                    auto is_chunked_token = [](std::string_view token) -> bool {
                        constexpr std::string_view chunked = "chunked";
                        if (token.size() != chunked.size()) return false;
                        for (std::size_t i = 0; i < chunked.size(); ++i) {
                            if (ascii_lower(token[i]) != chunked[i]) return false;
                        }
                        return true;
                    };

                    for (const auto &header: transfer_encoding_headers) {
                        std::string_view value = header.value;
                        while (!value.empty()) {
                            const std::size_t comma = value.find(',');
                            std::string_view token = (comma == std::string_view::npos) ? value : value.substr(0, comma);
                            token = trim_view(token);

                            if (token.empty()) { return fail(Status::BAD_REQUEST, "Invalid Transfer-Encoding"); }

                            if (is_chunked_token(token)) {
                                has_chunked = true;
                            } else {
                                has_other_encoding = true;
                            }

                            if (comma == std::string_view::npos) break;
                            value.remove_prefix(comma + 1);
                        }
                    }


                    if (has_transfer_encoding) {
                        if (response.metadata.version != VERSION::HTTP_1_1) {
                            return fail(Status::BAD_REQUEST, "Transfer-Encoding not allowed");
                        }
                        if (!has_chunked || has_other_encoding) {
                            return fail(Status::BAD_REQUEST, "Unsupported Transfer-Encoding");
                        }
                    }

                    if (has_chunked && content_length_seen) {
                        return fail(Status::BAD_REQUEST, "Both Transfer-Encoding and Content-Length present");
                    }


                    const std::uint16_t status = response.metadata.status_code;
                    const bool status_no_body = (status >= 100 && status < 200) || status == 204 || status == 304;

                    if (status_no_body) {
                        ctx.after_headers = AfterHeaders::COMPLETE;
                        state = STATE::HEADERS_DONE;
                        break;
                    }

                    if (has_chunked) {
                        ctx.current_state_size = 0;
                        ctx.after_headers = AfterHeaders::CHUNKED;
                        state = STATE::HEADERS_DONE;
                        break;
                    }


                    if (content_length_seen) {
                        if (content_length_value > response.policy.max_body_size) {
                            return fail(Status::PAYLOAD_TOO_LARGE, "Body size too big");
                        }

                        ctx.body_read_size = content_length_value;

                        ctx.after_headers =
                                (content_length_value == 0) ? AfterHeaders::COMPLETE : AfterHeaders::CONTENT_LENGTH;

                        state = STATE::HEADERS_DONE;
                        break;
                    }


                    ctx.after_headers = AfterHeaders::UNTIL_CLOSE;
                    state = STATE::HEADERS_DONE;
                    break;
                }


                case STATE::HEADERS_DONE: {
                    switch (ctx.after_headers) {
                        case AfterHeaders::COMPLETE:
                            state = STATE::COMPLETE;
                            break;

                        case AfterHeaders::CHUNKED:
                            ctx.kv_buffer.first.clear();
                            ctx.current_state_size = 0;
                            ctx.body_read_size = 0;
                            state = STATE::DATA_CHUNKED_SIZE;
                            break;

                        case AfterHeaders::CONTENT_LENGTH:
                            ctx.current_state_size = 0;
                            state = STATE::DATA_CONTENT_LENGTH;
                            break;

                        case AfterHeaders::UNTIL_CLOSE:
                            state = STATE::BODY_UNTIL_CLOSE;
                            break;

                        default:
                            return fail(Status::BAD_REQUEST, "Invalid after_headers");
                    }
                    break;
                }

                case STATE::DATA_CONTENT_LENGTH: {
                    auto &content_length = ctx.body_read_size;
                    std::size_t already = static_cast<std::size_t>(ctx.current_state_size);

                    const std::size_t remaining = content_length - already;
                    const std::size_t available = static_cast<std::size_t>(end - begin);
                    const std::size_t take = (available < remaining) ? available : remaining;

                    if (already >= content_length) break;

                    // TODO: memcpy?
                    // request.body.append(static_cast<const char *>(begin), take);
                    // MSVC?
                    response.body.append(&*begin, take);

                    begin += take;
                    ctx.current_state_size += take;
                    ctx.body_bytes_read += take;

                    if (ctx.current_state_size == content_length) {
                        state = STATE::COMPLETE;
                        return {};
                    } else if (ctx.current_state_size > response.policy.max_body_size ||
                               ctx.current_state_size > content_length) {
                        return fail(Status::PAYLOAD_TOO_LARGE, "Body size too big");
                    }

                    break;
                }
                case STATE::DATA_CHUNKED_SIZE: {
                    while (begin != end) {
                        if (std::isxdigit(*begin)) {
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
                            return fail(Status::BAD_REQUEST, "Unknown symbol in chunked size");
                        }
                    }
                    break;
                }
                case STATE::DATA_CHUNKED_SIZE_CRLF: {
                    if (*begin == '\n') {
                        std::size_t size = 0;
                        if (!parse_hex_size(ctx.kv_buffer.first, size)) {
                            return fail(Status::BAD_REQUEST, "Invalid chunk size");
                        }
                        ctx.body_read_size = size;
                        ctx.kv_buffer.first.clear();
                        ctx.current_state_size = 0;
                        ++begin;
                        ++ctx.body_bytes_read;
                        state = STATE::DATA_CHUNKED_DATA;
                        if (ctx.body_read_size == 0) { state = STATE::DATA_CHUNKED_LAST_CR; }
                    } else {
                        return fail(Status::BAD_REQUEST, "Missing LF in chunked size");
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

                    response.body.append(&*begin, take);

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
                    if (*begin != '\r') { return fail(Status::BAD_REQUEST, "Missing CR after chunk data"); }
                    ++begin;
                    ++ctx.body_bytes_read;
                    state = STATE::DATA_CHUNKED_DATA_LF;
                }
                case STATE::DATA_CHUNKED_DATA_LF: {
                    if (begin == end) break;
                    if (*begin != '\n') { return fail(Status::BAD_REQUEST, "Missing LF after chunk data"); }
                    ++begin;
                    ++ctx.body_bytes_read;

                    state = STATE::DATA_CHUNK_DONE;
                    // return {};
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
                        return fail(Status::BAD_REQUEST, "Missing CR DATA_CHUNKED_LAST_CR");
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
                        return fail(Status::BAD_REQUEST, "Missing LF DATA_CHUNKED_LAST_LF");
                    }
                    [[fallthrough]];
                }
                case STATE::DATA_DONE: {
                    if (begin != end) { return fail(Status::BAD_REQUEST, "Trailers unsupported yet"); }
                    state = STATE::COMPLETE;
                    break;
                }
                case STATE::TRAILER_KEY: {
                    return fail(Status::BAD_REQUEST, "Trailers unsupported yet, how did we get here?!");
                    break;
                }

                case STATE::TRAILER_VALUE: {
                    return fail(Status::BAD_REQUEST, "Trailers unsupported yet, how did we get here?!");
                    break;
                }

                case STATE::TRAILER_CR: {
                    return fail(Status::BAD_REQUEST, "Trailers unsupported yet, how did we get here?!");
                    break;
                }

                case STATE::TRAILER_LF: {
                    return fail(Status::BAD_REQUEST, "Trailers unsupported yet, how did we get here?!");
                    break;
                }

                case STATE::TRAILERS_DONE: {
                    return fail(Status::BAD_REQUEST, "Trailers unsupported yet, how did we get here?!");
                    break;
                }

                case STATE::BODY_UNTIL_CLOSE: {
                    const std::size_t avail = static_cast<std::size_t>(end - begin);
                    if (response.body.size() + avail > response.policy.max_body_size) {
                        return fail(Status::PAYLOAD_TOO_LARGE, "Body size too big");
                    }

                    response.body.append(&*begin, avail);
                    begin = end;
                    state = STATE::COMPLETE;
                    break;
                }


                case STATE::COMPLETE:
                    state = STATE::STATUS_VERSION;
                    break;

                case STATE::FAILED:
                    begin = end;
                    return fail(Status::BAD_REQUEST, "Parser in failed state");

                default:
                    return fail(Status::BAD_REQUEST, "Invalid parser state");
            }
        }
        return {};
    }


    ResponseParser::ParserContext &ResponseParser::getContext() { return this->context_; }

}// namespace usub::unet::http::v1
