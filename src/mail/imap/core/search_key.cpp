#include "unet/mail/imap/core/search_key.hpp"

#include <cctype>

namespace usub::unet::mail::imap::core {
    namespace {

        [[nodiscard]] bool isAtomChar(unsigned char c) noexcept {
            if (c <= 0x1F || c == 0x7F) { return false; }
            switch (c) {
                case '(':
                case ')':
                case '{':
                case ' ':
                case '%':
                case '*':
                case '"':
                case '\\':
                    return false;
                default:
                    return true;
            }
        }

        [[nodiscard]] bool validKey(std::string_view key) noexcept {
            if (key.empty()) { return false; }
            for (unsigned char c: key) {
                if (!isAtomChar(c) || std::islower(c) != 0) { return false; }
            }
            return true;
        }

        [[nodiscard]] std::string quote(std::string_view input) {
            std::string out;
            out.reserve(input.size() + 2);
            out.push_back('"');
            for (char ch: input) {
                if (ch == '\\' || ch == '"') { out.push_back('\\'); }
                out.push_back(ch);
            }
            out.push_back('"');
            return out;
        }

    }// namespace

    std::expected<std::string, Error> serializeSearchKeys(std::span<const SearchKey> keys) {
        if (keys.empty()) {
            return std::unexpected(
                    Error{.code = ErrorCode::InvalidInput, .message = "at least one SEARCH key is required"});
        }

        std::string out;
        bool first_key = true;
        for (const auto &key: keys) {
            if (!validKey(key.name)) {
                return std::unexpected(
                        Error{.code = ErrorCode::InvalidToken, .message = "invalid SEARCH key atom"});
            }

            if (!first_key) { out.push_back(' '); }
            first_key = false;

            out.append(key.name);
            for (const auto &argument: key.arguments) {
                if (argument.find('\r') != std::string::npos || argument.find('\n') != std::string::npos) {
                    return std::unexpected(Error{.code = ErrorCode::InvalidInput,
                                                 .message = "SEARCH key argument must not contain CRLF"});
                }
                out.push_back(' ');
                out.append(quote(argument));
            }
        }

        return out;
    }

}// namespace usub::unet::mail::imap::core
