#include <expected>
#include <optional>
#include <string_view>

#include "unet/http/error.hpp"
#include "unet/http/response.hpp"
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

        while (begin != end) {
            switch (state) {
                case STATE::PROTOCOL:
                    break;
                case STATE::STATUS_CODE:
                    break;
                case STATE::REASON_PHRASE:
                    break;
                case STATE::VERSION:
                    break;
                case STATE::REQUEST_LINE_CRLF_HTTP_0_9:
                    break;
                case STATE::REQUEST_LINE_CRLF:
                    break;
                case STATE::HEADER_KEY:
                    break;
                case STATE::HEADER_VALUE:
                    break;
                case STATE::HEADER_CR:
                    break;
                case STATE::HEADER_LF:
                    break;
                case STATE::HEADERS_CRLF:
                    break;
                case STATE::HEADERS_VALIDATION:
                    break;
                case STATE::HEADERS_DONE:
                    break;
                case STATE::DATA_CONTENT_LENGTH:
                    break;
                case STATE::DATA_CHUNKED_SIZE:
                    break;
                case STATE::DATA_CHUNKED_SIZE_CRLF:
                    break;
                case STATE::DATA_CHUNKED_DATA:
                    break;
                case STATE::DATA_CHUNKED_DATA_CR:
                    break;
                case STATE::DATA_CHUNKED_DATA_LF:
                    break;
                case STATE::DATA_CHUNKED_LAST_CR:
                    break;
                case STATE::DATA_CHUNKED_LAST_LF:
                    break;
                case STATE::DATA_CHUNK_DONE:
                    break;
                case STATE::DATA_DONE:
                    break;
                case STATE::TRAILER_KEY:
                    break;
                case STATE::TRAILER_VALUE:
                    break;
                case STATE::TRAILER_CR:
                    break;
                case STATE::TRAILER_LF:
                    break;
                case STATE::TRAILERS_DONE:
                    break;
                case STATE::FAILED: {
                };
                case STATE::COMPLETE: {
                    // Result r;
                    // r.done = true;
                    // r.need_more = false;
                    // r.response = std::move(resp_);
                    // reset_for_next();
                    // return r;
                }
            }
        }
    }


    ResponseParser::ParserContext &ResponseParser::getContext() { return this->context_; }

}// namespace usub::unet::http::v1
