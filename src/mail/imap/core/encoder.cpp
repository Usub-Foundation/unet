#include "unet/mail/imap/core/encoder.hpp"

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

        [[nodiscard]] bool isTagChar(unsigned char c) noexcept {
            return std::isalnum(c) != 0;
        }

        [[nodiscard]] bool validTag(std::string_view tag) noexcept {
            if (tag.empty()) { return false; }
            for (unsigned char c: tag) {
                if (!isTagChar(c)) { return false; }
            }
            return true;
        }

        [[nodiscard]] bool validAtom(std::string_view atom) noexcept {
            if (atom.empty()) { return false; }
            for (unsigned char c: atom) {
                if (!isAtomChar(c)) { return false; }
            }
            return true;
        }

        [[nodiscard]] std::string quote(std::string_view input) {
            std::string out;
            out.reserve(input.size() + 2);
            out.push_back('"');
            for (char ch: input) {
                if (ch == '"' || ch == '\\') { out.push_back('\\'); }
                out.push_back(ch);
            }
            out.push_back('"');
            return out;
        }

        [[nodiscard]] std::expected<std::string, Error> encodeValue(const Value &value) {
            const auto &data = value.data;

            if (const auto *atom = std::get_if<Atom>(&data)) {
                if (!validAtom(atom->value)) {
                    return std::unexpected(
                            Error{.code = ErrorCode::InvalidToken, .message = "invalid IMAP atom argument"});
                }
                return atom->value;
            }

            if (const auto *number = std::get_if<Number>(&data)) { return std::to_string(*number); }

            if (const auto *string = std::get_if<String>(&data)) {
                switch (string->form) {
                    case String::Form::Quoted:
                        return quote(string->value);
                    case String::Form::SynchronizingLiteral:
                        return "{" + std::to_string(string->value.size()) + "}\r\n" + string->value;
                    case String::Form::NonSynchronizingLiteral:
                        return "{" + std::to_string(string->value.size()) + "+}\r\n" + string->value;
                }
            }

            if (const auto *literal8 = std::get_if<Literal8>(&data)) {
                std::string out = "~{" + std::to_string(literal8->value.size()) + "}\r\n";
                out.append(reinterpret_cast<const char *>(literal8->value.data()), literal8->value.size());
                return out;
            }

            if (std::holds_alternative<NIL>(data)) { return std::string{"NIL"}; }

            if (const auto *list = std::get_if<ParenthesizedList>(&data)) {
                std::string out{"("};
                bool first = true;
                for (const auto &child: *list) {
                    auto encoded = encodeValue(child);
                    if (!encoded) { return std::unexpected(encoded.error()); }
                    if (!first) { out.push_back(' '); }
                    first = false;
                    out.append(*encoded);
                }
                out.push_back(')');
                return out;
            }

            return std::unexpected(Error{.code = ErrorCode::Unsupported,
                                         .message = "unsupported IMAP command argument type"});
        }

    }// namespace

    std::string_view commandToString(COMMAND command) noexcept {
        switch (command) {
            case COMMAND::CAPABILITY:
                return "CAPABILITY";
            case COMMAND::NOOP:
                return "NOOP";
            case COMMAND::LOGOUT:
                return "LOGOUT";
            case COMMAND::STARTTLS:
                return "STARTTLS";
            case COMMAND::AUTHENTICATE:
                return "AUTHENTICATE";
            case COMMAND::LOGIN:
                return "LOGIN";
            case COMMAND::ENABLE:
                return "ENABLE";
            case COMMAND::SELECT:
                return "SELECT";
            case COMMAND::EXAMINE:
                return "EXAMINE";
            case COMMAND::CREATE:
                return "CREATE";
            case COMMAND::DELETE:
                return "DELETE";
            case COMMAND::RENAME:
                return "RENAME";
            case COMMAND::SUBSCRIBE:
                return "SUBSCRIBE";
            case COMMAND::UNSUBSCRIBE:
                return "UNSUBSCRIBE";
            case COMMAND::LIST:
                return "LIST";
            case COMMAND::NAMESPACE:
                return "NAMESPACE";
            case COMMAND::STATUS:
                return "STATUS";
            case COMMAND::APPEND:
                return "APPEND";
            case COMMAND::IDLE:
                return "IDLE";
            case COMMAND::CLOSE:
                return "CLOSE";
            case COMMAND::UNSELECT:
                return "UNSELECT";
            case COMMAND::EXPUNGE:
                return "EXPUNGE";
            case COMMAND::SEARCH:
                return "SEARCH";
            case COMMAND::FETCH:
                return "FETCH";
            case COMMAND::STORE:
                return "STORE";
            case COMMAND::COPY:
                return "COPY";
            case COMMAND::MOVE:
                return "MOVE";
            case COMMAND::UID:
                return "UID";
        }
        return {};
    }

    std::expected<std::string, Error> CommandEncoder::encode(const CommandRequest &request) {
        if (!validTag(request.tag)) {
            return std::unexpected(Error{.code = ErrorCode::InvalidToken, .message = "invalid command tag"});
        }

        const std::string_view command = commandToString(request.command);
        if (command.empty()) {
            return std::unexpected(Error{.code = ErrorCode::InvalidToken, .message = "unknown IMAP command"});
        }

        std::string out;
        out.reserve(64);
        out.append(request.tag);
        out.push_back(' ');
        out.append(command);

        for (const auto &argument: request.arguments) {
            auto encoded = encodeValue(argument);
            if (!encoded) { return std::unexpected(encoded.error()); }
            out.push_back(' ');
            out.append(*encoded);
        }

        out.append("\r\n");
        return out;
    }

    std::expected<std::string, Error> CommandEncoder::encodeSimple(std::string_view tag, COMMAND command) {
        CommandRequest request{};
        request.tag.assign(tag.data(), tag.size());
        request.command = command;
        return encode(request);
    }

    std::expected<std::string, Error>
    CommandEncoder::encodeLogin(std::string_view tag, std::string_view username, std::string_view password) {
        CommandRequest request{};
        request.tag.assign(tag.data(), tag.size());
        request.command = COMMAND::LOGIN;
        request.arguments = {
                Value{.data = String{.form = String::Form::Quoted, .value = std::string(username)}},
                Value{.data = String{.form = String::Form::Quoted, .value = std::string(password)}},
        };
        return encode(request);
    }

}// namespace usub::unet::mail::imap::core
