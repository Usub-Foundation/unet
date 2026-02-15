#include "unet/http/v1/wire/request_parser.hpp"

// TODO: Recheck on all that uses ctx....

namespace usub::unet::http::v1 {
    namespace {

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
                    if (asciiLower(c) != token[j]) {
                        match = false;
                        break;
                    }
                }
                if (match) return true;
                while (i < value.size() && value[i] != ',') { ++i; }
            }
            return false;
        }

    }// namespace

    std::expected<Request, ParseError> RequestParser::parse(const std::string_view raw_request) {
        RequestParser parser;
        Request request;
        auto begin = raw_request.begin();
        auto end = raw_request.end();
        for (;;) {
            auto result = parser.step(request, begin, end);
            if (!result) { return std::unexpected(result.error()); }
            if (parser.context_.state == STATE::COMPLETE) { return request; }
            if (begin == end) {
                ParseError err{ParseError::CODE::GENERIC_ERROR, STATUS_CODE::BAD_REQUEST, "Incomplete request", {}};
                return std::unexpected(err);
            }
        }
    }

    std::expected<ParseStep, ParseError> RequestParser::step(Request &request, std::string_view::const_iterator &begin,
                                                             const std::string_view::const_iterator end) {
        ParseStep rv{};
        auto &ctx = this->context_;
        auto &state = ctx.state;
        // if (state == STATE::HEADERS_DONE) { state = state; }
        using Status = usub::unet::http::STATUS_CODE;

        // TODO future reimplementation
        auto fail = [&](Status status, std::string_view message) -> std::expected<ParseStep, ParseError> {
            ParseError err{};
            err.code = ParseError::CODE::GENERIC_ERROR;
            err.expected_status = status;
            err.message = std::string(message);
            //TODO: Rethink
            // std::size_t remaining = static_cast<std::size_t>(e - it);
            // std::size_t copy_len = remaining < err.tail.size() ? remaining : err.tail.size();
            // std::memset(err.tail.data(), 0, err.tail.size());
            // if (copy_len > 0) {
            //     std::memcpy(err.tail.data(), it, copy_len);
            // }
            state = STATE::FAILED;
            // begin = it;
            return std::unexpected(err);
        };

        while (begin != end) {
            switch (state) {
                case STATE::METHOD_TOKEN: {
                    auto &method = request.metadata.method_token;
                    // TODO: im gonna leave it here for now, because it's 5 o'clock, but hell one time I return to LLMS
                    if (method.empty()) {
                        ctx.headers_size = 0;
                        ctx.body_bytes_read = 0;
                        // ctx.chunk_bytes_read = 0;
                        ctx.current_state_size = 0;
                        ctx.kv_buffer = {};
                        request.metadata.uri = {};
                        request.headers = {};
                        request.body.clear();
                    }
                    while (begin != end) {
                        if (isTchar(*begin)) [[likely]] {
                            method.push_back(static_cast<char>(*begin));
                            ++begin;
                            ++ctx.current_state_size;
                        } else if (*begin == ' ') {
                            if (method.empty()) { return fail(Status::BAD_REQUEST, "Empty method token"); }
                            ++begin;
                            state = STATE::URI;
                            ctx.current_state_size = 0;
                            break;
                        } else {
                            return fail(Status::BAD_REQUEST, "Invalid method token");
                        }
                        if (ctx.current_state_size > max_method_token_size) {
                            return fail(Status::BAD_REQUEST, "Method token too big");
                        }
                    }
                    break;
                }
                case STATE::URI: {
                    if (begin == end) break;
                    if (*begin == '/') {
                        state = STATE::ORIGIN_PATH;
                    } else if (*begin == '*') {
                        return fail(Status::BAD_REQUEST, "Unsupported");
                        state = STATE::ASTERISK_FORM;
                    } else if (isAlpha(*begin)) {
                        return fail(Status::BAD_REQUEST, "Unsupported");
                        state = STATE::ABSOLUTE_FORM;
                    } else {
                        return fail(Status::BAD_REQUEST, "Unsupported");
                        state = STATE::AUTHORITY_FORM;
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
                            return fail(Status::BAD_REQUEST, "Fragment is diallowed");
                            state = STATE::ORIGIN_FRAGMENT;
                            ++begin;
                            break;
                        } else if (*begin == ' ') {
                            state = STATE::VERSION;
                            ++begin;
                            ctx.current_state_size = 0;
                            break;
                        } else if (*begin == '\r') {
                            return fail(Status::BAD_REQUEST, "HTTP/0.9 not supported yet");
                        } else {
                            return fail(Status::BAD_REQUEST, "Invalid path character");
                        }
                        if (ctx.current_state_size >= usub::unet::http::max_uri_size) {
                            return fail(Status::URI_TOO_LONG, "URI too long");
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
                            return fail(Status::BAD_REQUEST, "Fragment is diallowed");
                            state = STATE::ORIGIN_FRAGMENT;
                            ++begin;
                            break;
                        } else if (*begin == ' ') {
                            state = STATE::VERSION;
                            ++begin;
                            ctx.current_state_size = 0;
                            break;
                        } else {
                            return fail(Status::BAD_REQUEST, "Invalid query character");
                        }
                        if (ctx.current_state_size >= usub::unet::http::max_uri_size) {
                            return fail(Status::URI_TOO_LONG, "URI too long");
                        }
                    }
                    break;
                }
                case STATE::ORIGIN_FRAGMENT: {
                    return fail(Status::BAD_REQUEST, "Fragment, How did we get here?");
                    break;
                }
                case STATE::ABSOLUTE_FORM: {
                    return fail(Status::BAD_REQUEST, "Absolute form, How did we get here");
                }
                case STATE::AUTHORITY_FORM: {
                    return fail(Status::BAD_REQUEST, "Authority form, How did we get here");
                }
                case STATE::ASTERISK_FORM: {
                    return fail(Status::BAD_REQUEST, "Asterisk form, How did we get here");
                    if (request.metadata.method_token != "OPTIONS") {
                        return fail(Status::BAD_REQUEST, "Origin form without OPTIONS method");
                    }
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
                            return fail(Status::BAD_REQUEST, "Wrong Version");
                        }
                        if (ctx.current_state_size > 8) { return fail(Status::BAD_REQUEST, "Version too large"); }
                    }
                    [[fallthrough]];
                }
                case STATE::REQUEST_LINE_CRLF: {
                    if (begin == end) [[unlikely]] { return rv; }
                    auto &version_buf = ctx.kv_buffer.first;
                    if (*begin != '\n') { return fail(Status::BAD_REQUEST, "Missing LF"); }
                    ++begin;
                    if (version_buf == "HTTP/1.1") {
                        request.metadata.version = VERSION::HTTP_1_1;
                    } else if (version_buf == "HTTP/1.0") {
                        request.metadata.version = VERSION::HTTP_1_0;
                    } else {
                        return fail(Status::BAD_REQUEST, "Unknown version");
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
                    request.headers.addHeader(std::move(key), std::move(value));
                    if (begin == end) { return rv; }
                    if (isTchar(*begin)) {
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

                    if (!request.headers.contains("host")) { return fail(Status::BAD_REQUEST, "Missing Host header"); }

                    request.metadata.authority = request.headers.all("host")[0].value;

                    const bool method_no_body =
                            request.metadata.method_token == "GET" || request.metadata.method_token == "HEAD" ||
                            request.metadata.method_token == "OPTIONS" || request.metadata.method_token == "TRACE";

                    const auto content_length_headers = request.headers.all("content-length");
                    const auto transfer_encoding_headers = request.headers.all("transfer-encoding");
                    const bool has_transfer_encoding = !transfer_encoding_headers.empty();

                    std::size_t content_length_value = 0;
                    bool content_length_seen = false;

                    auto trim_view = [](std::string_view value) -> std::string_view {
                        std::size_t start = 0;
                        std::size_t end = value.size();
                        while (start < end && (value[start] == ' ' || value[start] == '\t')) { ++start; }
                        while (end > start && (value[end - 1] == ' ' || value[end - 1] == '\t')) { --end; }
                        return std::string_view(value.data() + start, end - start);
                    };

                    auto parse_uint = [](std::string_view value, std::size_t &out) -> bool {
                        if (value.empty()) return false;
                        std::size_t result = 0;
                        for (char c: value) {
                            if (c < '0' || c > '9') { return false; }
                            std::size_t digit = static_cast<std::size_t>(c - '0');
                            if (result > (std::numeric_limits<std::size_t>::max() - digit) / 10) { return false; }
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
                            if (asciiLower(token[i]) != chunked[i]) return false;
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
                        if (request.metadata.version != VERSION::HTTP_1_1) {
                            return fail(Status::BAD_REQUEST, "Transfer-Encoding not allowed");
                        }
                        if (!has_chunked || has_other_encoding) {
                            return fail(Status::BAD_REQUEST, "Unsupported Transfer-Encoding");
                        }
                    }

                    if (has_chunked && content_length_seen) {
                        return fail(Status::BAD_REQUEST, "Both Transfer-Encoding and Content-Length present");
                    }

                    if (method_no_body) {
                        if (has_chunked) { return fail(Status::BAD_REQUEST, "Body not allowed for method"); }
                        if (content_length_seen && content_length_value != 0) {
                            return fail(Status::BAD_REQUEST, "Body not allowed for method");
                        }
                        state = STATE::COMPLETE;
                        rv.complete = true;
                        rv.kind = STEP::HEADERS;
                        return rv;
                    }

                    if (has_chunked) {
                        ctx.current_state_size = 0;
                        state = STATE::DATA_CHUNKED_SIZE;
                        break;
                    }

                    if (content_length_seen) {
                        ctx.body_read_size = content_length_value;
                        if (content_length_value == 0) {
                            state = STATE::COMPLETE;
                            rv.complete = true;
                            rv.kind = STEP::HEADERS;
                            return rv;
                        } else {
                            state = STATE::DATA_CONTENT_LENGTH;
                        }
                        break;
                    }

                    state = STATE::DATA_CONTENT_LENGTH;
                    rv.kind = STEP::HEADERS;
                    break;
                }
                // case STATE::HEADERS_DONE: {
                //     // Realistically we dont get there... but so the parser can work standalone
                //     state = this->context_.post_header_middleware_state;
                //     break;
                // }
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
                    request.body.append(&*begin, take);

                    begin += take;
                    ctx.current_state_size += take;
                    ctx.body_bytes_read += take;

                    if (ctx.current_state_size == content_length) {
                        state = STATE::COMPLETE;
                        rv.kind = STEP::COMPLETE;
                        return rv;
                    } else if (ctx.current_state_size > content_length) {
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
                        ctx.body_read_size =
                                std::stoull(ctx.kv_buffer.first, nullptr,
                                            16);// this might throw, TODO: Replace with some not throwing alt
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

                    request.body.append(static_cast<char>(*begin), take);

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
                    rv.kind = STEP::BODY;
                    return rv;
                }
                case STATE::DATA_CHUNK_DONE: {
                    ctx.current_state_size = 0;
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
                    if (begin == end) { return rv; }
                    if (*begin == '\n') {
                        ++begin;
                        ++ctx.body_bytes_read;
                        state = STATE::DATA_DONE;
                    } else {
                        return fail(Status::BAD_REQUEST, "Missing LF DATA_CHUNKED_LAST_LF");
                    }
                    [[fallthrough]];
                }
                case STATE::DATA_DONE:
                    if (begin != end) { return fail(Status::BAD_REQUEST, "Trailers unsupported yet"); }
                    rv.kind = STEP::COMPLETE;
                    return rv;
                    // TODO: Think if content-length should also go here, for some kind of check
                    break;
                case STATE::COMPLETE:
                    // TODO: Clear request/response and parser state
                    this->context_ = {};
                    request = {};
                    // state = STATE::METHOD_TOKEN;
                    break;
                case STATE::FAILED:
                    begin = end;
                    return fail(Status::BAD_REQUEST, "Parser in failed state");
                default:
                    return fail(Status::BAD_REQUEST, "Invalid parser state");
            }
        }
        return rv;
    }

    RequestParser::ParserContext &RequestParser::getContext() { return this->context_; }
}// namespace usub::unet::http::v1
