#include "unet/mail/imap/core/parser.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <limits>
#include <string>

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

        [[nodiscard]] bool isTagChar(unsigned char c) noexcept {
            return std::isalnum(c) != 0;
        }

        [[nodiscard]] std::string_view trimLeft(std::string_view v) noexcept {
            std::size_t i = 0;
            while (i < v.size() && v[i] == ' ') { ++i; }
            return v.substr(i);
        }

        [[nodiscard]] bool iequals(std::string_view lhs, std::string_view rhs) noexcept {
            if (lhs.size() != rhs.size()) { return false; }
            for (std::size_t i = 0; i < lhs.size(); ++i) {
                unsigned char a = static_cast<unsigned char>(lhs[i]);
                unsigned char b = static_cast<unsigned char>(rhs[i]);
                if (std::toupper(a) != std::toupper(b)) { return false; }
            }
            return true;
        }

        [[nodiscard]] std::optional<ResponseCondition> conditionFromToken(std::string_view token) {
            if (iequals(token, "OK")) { return ResponseCondition::OK; }
            if (iequals(token, "NO")) { return ResponseCondition::NO; }
            if (iequals(token, "BAD")) { return ResponseCondition::BAD; }
            if (iequals(token, "PREAUTH")) { return ResponseCondition::PREAUTH; }
            if (iequals(token, "BYE")) { return ResponseCondition::BYE; }
            return std::nullopt;
        }

        [[nodiscard]] bool allTagChars(std::string_view token) noexcept {
            if (token.empty()) { return false; }
            for (unsigned char c: token) {
                if (!isTagChar(c)) { return false; }
            }
            return true;
        }

        [[nodiscard]] bool allAtomChars(std::string_view token) noexcept {
            if (token.empty()) { return false; }
            for (unsigned char c: token) {
                if (!isAtomChar(c)) { return false; }
            }
            return true;
        }

        [[nodiscard]] std::pair<std::string_view, std::string_view> splitToken(std::string_view input) noexcept {
            input = trimLeft(input);
            if (input.empty()) { return {{}, {}}; }

            std::size_t i = 0;
            while (i < input.size() && input[i] != ' ') { ++i; }
            if (i == input.size()) { return {input, {}}; }
            return {input.substr(0, i), trimLeft(input.substr(i + 1))};
        }

        [[nodiscard]] std::expected<StatusInfo, Error> parseStatus(std::string_view input, std::size_t base_offset) {
            auto [condition_token, rest] = splitToken(input);
            if (condition_token.empty()) {
                return std::unexpected(Error{.code = ErrorCode::InvalidSyntax,
                                             .message = "missing status condition token",
                                             .offset = base_offset});
            }

            auto condition = conditionFromToken(condition_token);
            if (!condition.has_value()) {
                return std::unexpected(Error{.code = ErrorCode::InvalidToken,
                                             .message = "unknown status condition",
                                             .offset = base_offset});
            }

            StatusInfo status{};
            status.condition = *condition;

            if (!rest.empty() && rest.front() == '[') {
                const std::size_t close = rest.find(']');
                if (close == std::string_view::npos) {
                    return std::unexpected(Error{.code = ErrorCode::InvalidSyntax,
                                                 .message = "unterminated response code section",
                                                 .offset = base_offset + condition_token.size() + 1});
                }

                std::string_view code_payload = rest.substr(1, close - 1);
                auto [code_name, code_rest] = splitToken(code_payload);
                if (code_name.empty() || !allAtomChars(code_name)) {
                    return std::unexpected(Error{.code = ErrorCode::InvalidToken,
                                                 .message = "invalid response code atom",
                                                 .offset = base_offset + condition_token.size() + 2});
                }

                status.code = ResponseCode{.name = Atom{.value = std::string(code_name)}, .text = std::string(code_rest)};
                rest = trimLeft(rest.substr(close + 1));
            }

            status.text = std::string(rest);
            return status;
        }

        [[nodiscard]] std::expected<std::uint32_t, Error> parseUint32(std::string_view token, std::size_t offset) {
            if (token.empty()) {
                return std::unexpected(Error{.code = ErrorCode::InvalidToken,
                                             .message = "empty numeric token",
                                             .offset = offset});
            }

            std::uint64_t number = 0;
            const char *begin = token.data();
            const char *end = begin + token.size();
            auto [ptr, ec] = std::from_chars(begin, end, number);
            if (ec != std::errc() || ptr != end || number > std::numeric_limits<std::uint32_t>::max()) {
                return std::unexpected(Error{.code = ErrorCode::InvalidToken,
                                             .message = "invalid numeric token",
                                             .offset = offset});
            }
            return static_cast<std::uint32_t>(number);
        }

    }// namespace

    ResponseParser::ResponseParser(ParserLimits limits) : limits_(limits) {}

    std::expected<void, Error> ResponseParser::feed(std::string_view bytes) {
        if (bytes.empty()) { return {}; }
        if (buffer_.size() > limits_.max_buffer_bytes || bytes.size() > limits_.max_buffer_bytes - buffer_.size()) {
            return std::unexpected(Error{.code = ErrorCode::LimitExceeded,
                                         .message = "imap parser buffer limit exceeded",
                                         .offset = cursor_});
        }

        buffer_.append(bytes.data(), bytes.size());
        return {};
    }

    std::expected<std::optional<Response>, Error> ResponseParser::next() {
        if (cursor_ >= buffer_.size()) {
            compactBuffer();
            return std::optional<Response>{};
        }

        const std::string_view view(buffer_);
        const std::size_t line_end = findCrlf(view, cursor_);
        if (line_end == std::string_view::npos) {
            const std::size_t pending = view.size() - cursor_;
            if (pending > limits_.max_line_bytes) {
                return std::unexpected(Error{.code = ErrorCode::LimitExceeded,
                                             .message = "imap line exceeds parser limit",
                                             .offset = cursor_});
            }
            return std::optional<Response>{};
        }

        const std::size_t line_size = line_end - cursor_;
        if (line_size > limits_.max_line_bytes) {
            return std::unexpected(
                    Error{.code = ErrorCode::LimitExceeded, .message = "imap line too long", .offset = cursor_});
        }

        std::string_view line = view.substr(cursor_, line_size);
        const auto literal_spec = parseLiteralSuffix(line);

        if (literal_spec.has_value()) {
            if (literal_spec->bytes > limits_.max_literal_bytes) {
                return std::unexpected(Error{.code = ErrorCode::LimitExceeded,
                                             .message = "imap literal exceeds parser limit",
                                             .offset = cursor_});
            }

            const std::size_t header_end = line_end + 2;
            const std::size_t after_literal = header_end + literal_spec->bytes;
            if (after_literal > buffer_.size()) { return std::optional<Response>{}; }

            const std::size_t trailer_end = findCrlf(view, after_literal);
            if (trailer_end == std::string_view::npos) { return std::optional<Response>{}; }

            std::string literal = buffer_.substr(header_end, literal_spec->bytes);
            std::string_view trailer = view.substr(after_literal, trailer_end - after_literal);

            auto parsed = parseLineWithLiteral(line, literal, trailer);
            if (!parsed) { return std::unexpected(parsed.error()); }

            cursor_ = trailer_end + 2;
            parsed->raw.assign(line.data(), line.size());
            parsed->raw.append("\r\n");
            parsed->raw.append(literal);
            parsed->raw.append(trailer.data(), trailer.size());

            compactBuffer();
            return std::optional<Response>{std::move(*parsed)};
        }

        auto parsed = parseSimpleLine(line);
        if (!parsed) { return std::unexpected(parsed.error()); }

        cursor_ = line_end + 2;
        parsed->raw.assign(line.data(), line.size());
        compactBuffer();
        return std::optional<Response>{std::move(*parsed)};
    }

    void ResponseParser::reset() {
        buffer_.clear();
        cursor_ = 0;
    }

    std::size_t ResponseParser::bufferedBytes() const noexcept {
        if (cursor_ >= buffer_.size()) { return 0; }
        return buffer_.size() - cursor_;
    }

    std::expected<Response, Error> ResponseParser::parseSimpleLine(std::string_view line) const {
        if (line.empty()) {
            return std::unexpected(Error{.code = ErrorCode::InvalidSyntax, .message = "empty imap response line"});
        }

        if (line.front() == '+') {
            ContinuationResponse continuation{};
            if (line.size() == 1) {
                continuation.text = "";
            } else {
                if (line[1] != ' ') {
                    return std::unexpected(
                            Error{.code = ErrorCode::InvalidSyntax, .message = "invalid continuation response"});
                }
                continuation.text.assign(line.data() + 2, line.size() - 2);
            }
            Response response{};
            response.kind = Response::Kind::Continuation;
            response.data = std::move(continuation);
            return response;
        }

        if (line.front() == '*') {
            if (line.size() < 3 || line[1] != ' ') {
                return std::unexpected(Error{.code = ErrorCode::InvalidSyntax,
                                             .message = "untagged response must start with '* '"});
            }

            std::string_view payload = trimLeft(line.substr(2));
            auto [first, rest] = splitToken(payload);
            if (first.empty()) {
                return std::unexpected(Error{.code = ErrorCode::InvalidSyntax,
                                             .message = "missing untagged payload token",
                                             .offset = 2});
            }

            if (auto cond = conditionFromToken(first); cond.has_value()) {
                auto status = parseStatus(payload, 2);
                if (!status) { return std::unexpected(status.error()); }

                Response response{};
                response.kind = Response::Kind::Untagged;
                response.data = UntaggedResponse{.payload = UntaggedStatusResponse{.status = std::move(*status)}};
                return response;
            }

            if (!first.empty() && std::isdigit(static_cast<unsigned char>(first.front())) != 0) {
                auto number = parseUint32(first, 2);
                if (!number) { return std::unexpected(number.error()); }

                auto [atom, tail] = splitToken(rest);
                if (atom.empty() || !allAtomChars(atom)) {
                    return std::unexpected(
                            Error{.code = ErrorCode::InvalidToken, .message = "invalid untagged numeric atom"});
                }

                Response response{};
                response.kind = Response::Kind::Untagged;
                response.data = UntaggedResponse{
                        .payload = UntaggedNumericResponse{.number = *number,
                                                           .atom = Atom{.value = std::string(atom)},
                                                           .text = std::string(tail)}};
                return response;
            }

            if (!allAtomChars(first)) {
                return std::unexpected(
                        Error{.code = ErrorCode::InvalidToken, .message = "invalid untagged response atom"});
            }

            Response response{};
            response.kind = Response::Kind::Untagged;
            response.data = UntaggedResponse{.payload = UntaggedDataResponse{
                                               .atom = Atom{.value = std::string(first)},
                                               .text = std::string(rest),
                                           }};
            return response;
        }

        auto [tag, rest] = splitToken(line);
        if (tag.empty() || !allTagChars(tag)) {
            return std::unexpected(Error{.code = ErrorCode::InvalidToken, .message = "invalid response tag"});
        }

        auto status = parseStatus(rest, tag.size() + 1);
        if (!status) { return std::unexpected(status.error()); }

        Response response{};
        response.kind = Response::Kind::Tagged;
        response.data = TaggedResponse{.tag = Atom{.value = std::string(tag)}, .status = std::move(*status)};
        return response;
    }

    std::expected<Response, Error> ResponseParser::parseLineWithLiteral(std::string_view line, std::string_view literal,
                                                                        std::string_view trailer) const {
        auto literal_spec = parseLiteralSuffix(line);
        if (!literal_spec.has_value()) {
            return std::unexpected(Error{.code = ErrorCode::InvalidSyntax,
                                         .message = "internal parser error: missing literal suffix"});
        }

        const std::size_t marker_begin = line.rfind('{');
        if (marker_begin == std::string_view::npos) {
            return std::unexpected(Error{.code = ErrorCode::InvalidSyntax, .message = "invalid literal marker"});
        }

        std::string_view stripped_line = line.substr(0, marker_begin);
        if (!stripped_line.empty() && stripped_line.back() == ' ') { stripped_line.remove_suffix(1); }

        auto parsed = parseSimpleLine(stripped_line);
        if (!parsed) { return std::unexpected(parsed.error()); }

        if (parsed->kind != Response::Kind::Untagged) {
            return std::unexpected(Error{.code = ErrorCode::Unsupported,
                                         .message = "literal payload currently supports only untagged responses"});
        }

        auto &untagged = std::get<UntaggedResponse>(parsed->data);
        const auto literal_form = literal_spec->non_synchronizing ? String::Form::NonSynchronizingLiteral
                                                                   : String::Form::SynchronizingLiteral;

        if (auto *numeric = std::get_if<UntaggedNumericResponse>(&untagged.payload)) {
            numeric->literal = String{.form = literal_form, .value = std::string(literal)};
            if (!trailer.empty()) { numeric->text.append(trailer.data(), trailer.size()); }
            return parsed;
        }

        auto *data = std::get_if<UntaggedDataResponse>(&untagged.payload);
        if (!data) {
            return std::unexpected(Error{.code = ErrorCode::Unsupported,
                                         .message = "literal payload currently supports only untagged numeric/data responses"});
        }

        data->literal = String{.form = literal_form, .value = std::string(literal)};
        if (!trailer.empty()) { data->text.append(trailer.data(), trailer.size()); }
        return parsed;
    }

    std::optional<ResponseParser::LiteralSpec> ResponseParser::parseLiteralSuffix(std::string_view line) {
        if (line.empty() || line.back() != '}') { return std::nullopt; }

        const std::size_t open = line.rfind('{');
        if (open == std::string_view::npos || open + 2 >= line.size()) { return std::nullopt; }

        std::string_view payload = line.substr(open + 1, line.size() - open - 2);
        bool non_sync = false;
        if (!payload.empty() && payload.back() == '+') {
            non_sync = true;
            payload.remove_suffix(1);
        }

        if (payload.empty()) { return std::nullopt; }
        for (char c: payload) {
            if (std::isdigit(static_cast<unsigned char>(c)) == 0) { return std::nullopt; }
        }

        std::size_t number = 0;
        const char *begin = payload.data();
        const char *end = begin + payload.size();
        auto [ptr, ec] = std::from_chars(begin, end, number);
        if (ec != std::errc() || ptr != end) { return std::nullopt; }

        return LiteralSpec{.bytes = number, .non_synchronizing = non_sync};
    }

    std::size_t ResponseParser::findCrlf(std::string_view input, std::size_t from) noexcept {
        for (std::size_t i = from; i + 1 < input.size(); ++i) {
            if (input[i] == '\r' && input[i + 1] == '\n') { return i; }
        }
        return std::string_view::npos;
    }

    void ResponseParser::compactBuffer() {
        if (cursor_ == 0) { return; }

        if (cursor_ >= buffer_.size()) {
            buffer_.clear();
            cursor_ = 0;
            return;
        }

        if (cursor_ > 4096 && cursor_ > buffer_.size() / 2) {
            buffer_.erase(0, cursor_);
            cursor_ = 0;
        }
    }

}// namespace usub::unet::mail::imap::core
