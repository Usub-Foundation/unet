#pragma once

#include <algorithm>
#include <cctype>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "unet/mail/imap/core/error.hpp"
#include "unet/mail/imap/core/response.hpp"

namespace usub::unet::mail::imap::wire {
    namespace detail {

        [[nodiscard]] inline std::string trim(std::string_view value) {
            std::size_t begin = 0;
            while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) { ++begin; }
            std::size_t end = value.size();
            while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) { --end; }
            return std::string(value.substr(begin, end - begin));
        }

        [[nodiscard]] inline std::string upper(std::string_view value) {
            std::string out{value};
            std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
                return static_cast<char>(std::toupper(ch));
            });
            return out;
        }

        [[nodiscard]] inline bool iequals(std::string_view lhs, std::string_view rhs) {
            return upper(lhs) == upper(rhs);
        }

        [[nodiscard]] inline std::optional<std::size_t> parseLiteralSize(std::string_view line) {
            if (line.empty() || line.back() != '}') { return std::nullopt; }
            const auto open = line.rfind('{');
            if (open == std::string_view::npos || open + 2 > line.size()) { return std::nullopt; }
            std::size_t value = 0;
            for (std::size_t i = open + 1; i + 1 < line.size(); ++i) {
                if (!std::isdigit(static_cast<unsigned char>(line[i]))) { return std::nullopt; }
                value = value * 10 + static_cast<std::size_t>(line[i] - '0');
            }
            return value;
        }

        [[nodiscard]] inline std::optional<std::string> parseBracket(std::string_view value, std::size_t start,
                                                                      std::size_t &next) {
            if (start >= value.size() || value[start] != '[') { return std::nullopt; }
            const auto end = value.find(']', start + 1);
            if (end == std::string_view::npos) { return std::nullopt; }
            next = end + 1;
            return std::string(value.substr(start + 1, end - start - 1));
        }

        [[nodiscard]] inline std::optional<response::ResponseCode> parseResponseCode(std::string_view value,
                                                                                      std::size_t &cursor) {
            if (cursor >= value.size() || value[cursor] != '[') { return std::nullopt; }
            const auto bracket = parseBracket(value, cursor, cursor);
            if (!bracket.has_value()) { return std::nullopt; }
            const auto content = trim(*bracket);
            if (content.empty()) { return std::nullopt; }
            const auto space = content.find(' ');
            if (space == std::string::npos) { return response::ResponseCode{.name = content}; }
            return response::ResponseCode{.name = content.substr(0, space), .data = trim(content.substr(space + 1))};
        }

        template<typename T, typename Variant>
        struct variant_accepts : std::false_type {};

        template<typename T, typename... Ts>
        struct variant_accepts<T, std::variant<Ts...>> : std::bool_constant<(std::is_same_v<T, Ts> || ...)> {};

        template<typename Variant>
        [[nodiscard]] inline Variant parseStatusVariant(std::string_view status_name, std::string_view remainder,
                                                        bool tagged) {
            auto cursor = std::size_t{0};
            while (cursor < remainder.size()
                   && std::isspace(static_cast<unsigned char>(remainder[cursor])) != 0) { ++cursor; }
            auto code = parseResponseCode(remainder, cursor);
            while (cursor < remainder.size()
                   && std::isspace(static_cast<unsigned char>(remainder[cursor])) != 0) { ++cursor; }
            const std::string text = trim(remainder.substr(cursor));

            if (iequals(status_name, "OK")) { return Variant{response::Ok{.code = code, .text = text}}; }
            if (iequals(status_name, "NO")) { return Variant{response::No{.code = code, .text = text}}; }
            if (iequals(status_name, "BAD")) { return Variant{response::Bad{.code = code, .text = text}}; }
            if constexpr (variant_accepts<response::Preauth, Variant>::value) {
                if (!tagged && iequals(status_name, "PREAUTH")) {
                    return Variant{response::Preauth{.code = code, .text = text}};
                }
            }
            if constexpr (variant_accepts<response::Bye, Variant>::value) {
                if (!tagged && iequals(status_name, "BYE")) { return Variant{response::Bye{.code = code, .text = text}}; }
            }
            return Variant{response::GenericStatus{.name = upper(status_name), .code = code, .text = text}};
        }

        [[nodiscard]] inline std::vector<std::string> parseParenthesizedAtoms(std::string_view value) {
            std::vector<std::string> out;
            const auto open = value.find('(');
            const auto close = value.rfind(')');
            if (open == std::string_view::npos || close == std::string_view::npos || close <= open) { return out; }
            std::string current;
            for (std::size_t i = open + 1; i < close; ++i) {
                const char ch = value[i];
                if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
                    if (!current.empty()) {
                        out.push_back(current);
                        current.clear();
                    }
                    continue;
                }
                current.push_back(ch);
            }
            if (!current.empty()) { out.push_back(current); }
            return out;
        }

        [[nodiscard]] inline NString parseNStringToken(std::string_view token) {
            const auto trimmed = trim(token);
            if (iequals(trimmed, "NIL")) { return Nil{}; }
            if (trimmed.size() >= 2 && trimmed.front() == '"' && trimmed.back() == '"') {
                return trimmed.substr(1, trimmed.size() - 2);
            }
            return trimmed;
        }

        [[nodiscard]] inline response::ListData parseListData(std::string_view remainder) {
            response::ListData data{};
            const auto open = remainder.find('(');
            const auto close = remainder.find(')', open == std::string_view::npos ? 0 : open + 1);
            if (open == std::string_view::npos || close == std::string_view::npos) { return data; }

            data.item.attributes = parseParenthesizedAtoms(remainder.substr(open, close - open + 1));
            auto tail = trim(remainder.substr(close + 1));

            std::string delimiter_token;
            std::string mailbox_token;
            if (!tail.empty()) {
                if (tail.front() == '"') {
                    const auto end = tail.find('"', 1);
                    delimiter_token = tail.substr(0, end + 1);
                    mailbox_token = trim(tail.substr(end + 1));
                } else {
                    const auto space = tail.find(' ');
                    delimiter_token = tail.substr(0, space);
                    mailbox_token = space == std::string::npos ? std::string{} : trim(tail.substr(space + 1));
                }
            }

            data.item.hierarchy_delimiter = parseNStringToken(delimiter_token);
            if (mailbox_token.size() >= 2 && mailbox_token.front() == '"' && mailbox_token.back() == '"') {
                data.item.mailbox_name = mailbox_token.substr(1, mailbox_token.size() - 2);
            } else {
                data.item.mailbox_name = mailbox_token;
            }
            return data;
        }

        [[nodiscard]] inline response::StatusData parseStatusData(std::string_view remainder) {
            response::StatusData data{};
            const auto open = remainder.find('(');
            const auto close = remainder.rfind(')');
            if (open == std::string_view::npos || close == std::string_view::npos || close <= open) { return data; }

            auto mailbox = trim(remainder.substr(0, open));
            if (mailbox.size() >= 2 && mailbox.front() == '"' && mailbox.back() == '"') {
                mailbox = mailbox.substr(1, mailbox.size() - 2);
            }
            data.mailbox_name = mailbox;

            auto inside = trim(remainder.substr(open + 1, close - open - 1));
            while (!inside.empty()) {
                const auto name_end = inside.find(' ');
                if (name_end == std::string::npos) { break; }
                const auto name = inside.substr(0, name_end);
                inside = trim(inside.substr(name_end + 1));
                const auto value_end = inside.find(' ');
                const auto value_token = value_end == std::string::npos ? inside : inside.substr(0, value_end);
                if (!value_token.empty()) {
                    data.attributes.push_back(response::StatusAttribute{
                            .name = upper(name),
                            .value = static_cast<std::uint64_t>(std::stoull(std::string(value_token))),
                    });
                }
                if (value_end == std::string::npos) { break; }
                inside = trim(inside.substr(value_end + 1));
            }
            return data;
        }

        [[nodiscard]] inline response::SearchData parseSearchData(std::string_view remainder) {
            response::SearchData data{};
            auto tail = trim(remainder);
            while (!tail.empty()) {
                const auto next_space = tail.find(' ');
                const auto token = next_space == std::string::npos ? tail : tail.substr(0, next_space);
                const auto upper_token = upper(token);
                if (upper_token == "MIN" || upper_token == "MAX" || upper_token == "COUNT") {
                    tail = trim(next_space == std::string::npos ? std::string_view{} : tail.substr(next_space + 1));
                    const auto value_end = tail.find(' ');
                    const auto value_token = value_end == std::string::npos ? tail : tail.substr(0, value_end);
                    const auto value = static_cast<std::uint32_t>(std::stoul(std::string(value_token)));
                    if (upper_token == "MIN") {
                        data.min = value;
                    } else if (upper_token == "MAX") {
                        data.max = value;
                    } else {
                        data.count = value;
                    }
                    tail = value_end == std::string::npos ? std::string{} : trim(tail.substr(value_end + 1));
                    continue;
                }
                if (!token.empty() && std::isdigit(static_cast<unsigned char>(token.front())) != 0) {
                    data.matches.push_back(static_cast<std::uint32_t>(std::stoul(std::string(token))));
                }
                if (next_space == std::string::npos) { break; }
                tail = trim(tail.substr(next_space + 1));
            }
            return data;
        }

        [[nodiscard]] inline HeaderFields parseHeaderFields(std::string_view value) {
            HeaderFields fields{};
            std::size_t cursor = 0;
            while (cursor < value.size()) {
                const auto line_end = value.find("\r\n", cursor);
                const auto line = value.substr(cursor, line_end == std::string_view::npos ? value.size() - cursor
                                                                                           : line_end - cursor);
                cursor = line_end == std::string_view::npos ? value.size() : line_end + 2;

                if (line.empty()) { break; }
                const auto colon = line.find(':');
                if (colon == std::string_view::npos) { continue; }
                fields.values.emplace_back(trim(line.substr(0, colon)), trim(line.substr(colon + 1)));
            }
            return fields;
        }

        [[nodiscard]] inline std::string parseQuotedString(std::string_view value, std::size_t &cursor) {
            std::string out;
            ++cursor;
            while (cursor < value.size()) {
                const char ch = value[cursor++];
                if (ch == '\\' && cursor < value.size()) {
                    out.push_back(value[cursor++]);
                    continue;
                }
                if (ch == '"') { break; }
                out.push_back(ch);
            }
            return out;
        }

        [[nodiscard]] inline std::optional<std::string> messageDataString(const MessageDataValue &value) {
            if (const auto *text = value.asString(); text != nullptr) { return *text; }
            return std::nullopt;
        }

        [[nodiscard]] inline std::optional<std::uint64_t> messageDataNumber(const MessageDataValue &value) {
            if (const auto *number = value.asNumber(); number != nullptr) { return *number; }
            return std::nullopt;
        }

        [[nodiscard]] inline NString messageDataNString(const MessageDataValue &value) {
            if (value.isNil()) { return Nil{}; }
            if (const auto *text = value.asString(); text != nullptr) { return *text; }
            return Nil{};
        }

        [[nodiscard]] inline BodyFieldParameters parseBodyFieldParameters(const MessageDataValue &value) {
            BodyFieldParameters parameters{};
            const auto *list = value.asList();
            if (list == nullptr) { return parameters; }

            for (std::size_t i = 0; i + 1 < list->size(); i += 2) {
                const auto name = messageDataString((*list)[i]);
                if (!name.has_value()) { continue; }
                parameters.emplace_back(*name, messageDataNString((*list)[i + 1]));
            }
            return parameters;
        }

        [[nodiscard]] inline std::vector<std::string> parseBodyLanguages(const MessageDataValue &value) {
            std::vector<std::string> languages{};
            if (const auto one = messageDataString(value); one.has_value()) {
                languages.push_back(*one);
                return languages;
            }

            const auto *list = value.asList();
            if (list == nullptr) { return languages; }
            for (const auto &entry: *list) {
                if (const auto language = messageDataString(entry); language.has_value()) {
                    languages.push_back(*language);
                }
            }
            return languages;
        }

        [[nodiscard]] inline std::optional<BodyDisposition> parseBodyDisposition(const MessageDataValue &value) {
            const auto *list = value.asList();
            if (list == nullptr || list->empty()) { return std::nullopt; }

            const auto type = messageDataString((*list)[0]);
            if (!type.has_value()) { return std::nullopt; }

            BodyDisposition disposition{.type = *type};
            if (list->size() > 1) { disposition.parameters = parseBodyFieldParameters((*list)[1]); }
            return disposition;
        }

        [[nodiscard]] inline std::optional<Address> parseEnvelopeAddress(const MessageDataValue &value) {
            const auto *list = value.asList();
            if (list == nullptr || list->size() < 4) { return std::nullopt; }

            return Address{
                    .name = messageDataNString((*list)[0]),
                    .route = messageDataNString((*list)[1]),
                    .mailbox = messageDataNString((*list)[2]),
                    .host = messageDataNString((*list)[3]),
            };
        }

        [[nodiscard]] inline NAddressList parseEnvelopeAddressList(const MessageDataValue &value) {
            if (value.isNil()) { return Nil{}; }

            const auto *list = value.asList();
            if (list == nullptr) { return Nil{}; }

            AddressList addresses{};
            for (const auto &entry: *list) {
                auto address = parseEnvelopeAddress(entry);
                if (!address.has_value()) { continue; }
                addresses.push_back(std::move(*address));
            }
            return addresses;
        }

        [[nodiscard]] inline std::optional<Envelope> parseEnvelope(const MessageDataValue &value) {
            const auto *list = value.asList();
            if (list == nullptr || list->size() < 10) { return std::nullopt; }

            return Envelope{
                    .date = messageDataNString((*list)[0]),
                    .subject = messageDataNString((*list)[1]),
                    .from = parseEnvelopeAddressList((*list)[2]),
                    .sender = parseEnvelopeAddressList((*list)[3]),
                    .reply_to = parseEnvelopeAddressList((*list)[4]),
                    .to = parseEnvelopeAddressList((*list)[5]),
                    .cc = parseEnvelopeAddressList((*list)[6]),
                    .bcc = parseEnvelopeAddressList((*list)[7]),
                    .in_reply_to = messageDataNString((*list)[8]),
                    .message_id = messageDataNString((*list)[9]),
            };
        }

        [[nodiscard]] inline BodyStructure buildBodyStructure(const MessageDataValue &value) {
            const auto *list = value.asList();
            if (list == nullptr || list->empty()) { return BodyStructure{.data = MessageDataList{value}}; }

            if ((*list)[0].asList() != nullptr) {
                BodyMultipart multipart{};
                std::size_t cursor = 0;
                while (cursor < list->size() && (*list)[cursor].asList() != nullptr) {
                    multipart.parts.push_back(buildBodyStructure((*list)[cursor]));
                    ++cursor;
                }

                if (cursor < list->size()) {
                    if (const auto subtype = messageDataString((*list)[cursor]); subtype.has_value()) {
                        multipart.media_subtype = *subtype;
                    }
                    ++cursor;
                }
                if (cursor < list->size()) {
                    multipart.parameters = parseBodyFieldParameters((*list)[cursor]);
                    ++cursor;
                }
                if (cursor < list->size()) {
                    multipart.disposition = parseBodyDisposition((*list)[cursor]);
                    ++cursor;
                }
                if (cursor < list->size()) {
                    multipart.languages = parseBodyLanguages((*list)[cursor]);
                    ++cursor;
                }
                if (cursor < list->size()) { multipart.location = messageDataNString((*list)[cursor]); }
                return BodyStructure{.data = std::move(multipart)};
            }

            if (list->size() < 7) { return BodyStructure{.data = *list}; }

            BodyPartFields fields{};
            fields.media_type = messageDataString((*list)[0]).value_or(std::string{});
            fields.media_subtype = messageDataString((*list)[1]).value_or(std::string{});
            fields.parameters = parseBodyFieldParameters((*list)[2]);
            fields.id = messageDataNString((*list)[3]);
            fields.description = messageDataNString((*list)[4]);
            fields.transfer_encoding = messageDataString((*list)[5]).value_or(std::string{});
            fields.octet_count = messageDataNumber((*list)[6]);

            if (iequals(fields.media_type, "TEXT")) {
                BodyTextPart part{.fields = std::move(fields)};
                if (list->size() > 7) { part.line_count = messageDataNumber((*list)[7]); }
                return BodyStructure{.data = std::move(part)};
            }

            return BodyStructure{.data = BodyBasicPart{.fields = std::move(fields)}};
        }

        [[nodiscard]] inline std::string parseFetchItemName(std::string_view value, std::size_t &cursor) {
            const auto start = cursor;
            std::size_t bracket_depth = 0;
            std::size_t angle_depth = 0;
            while (cursor < value.size()) {
                const char ch = value[cursor];
                if (ch == '[') {
                    ++bracket_depth;
                    ++cursor;
                    continue;
                }
                if (ch == ']') {
                    if (bracket_depth > 0) { --bracket_depth; }
                    ++cursor;
                    continue;
                }
                if (ch == '<') {
                    ++angle_depth;
                    ++cursor;
                    continue;
                }
                if (ch == '>') {
                    if (angle_depth > 0) { --angle_depth; }
                    ++cursor;
                    continue;
                }
                if (bracket_depth == 0 && angle_depth == 0
                    && std::isspace(static_cast<unsigned char>(ch)) != 0) {
                    break;
                }
                ++cursor;
            }
            return std::string(value.substr(start, cursor - start));
        }

        [[nodiscard]] inline MessageDataValue parseFetchValue(std::string_view value, std::size_t &cursor,
                                                              std::string_view item_name);

        [[nodiscard]] inline MessageDataValue parseAtomLikeValue(std::string_view value, std::size_t &cursor) {
            const auto start = cursor;
            while (cursor < value.size() && std::isspace(static_cast<unsigned char>(value[cursor])) == 0
                   && value[cursor] != ')') {
                ++cursor;
            }
            const auto token = value.substr(start, cursor - start);
            if (iequals(token, "NIL")) { return MessageDataValue{.data = Nil{}}; }

            const bool all_digits = !token.empty()
                                    && std::all_of(token.begin(), token.end(), [](unsigned char ch) {
                                           return std::isdigit(ch) != 0;
                                       });
            if (all_digits) {
                return MessageDataValue{.data = static_cast<std::uint64_t>(std::stoull(std::string(token)))};
            }

            return MessageDataValue{.data = std::string(token)};
        }

        [[nodiscard]] inline MessageDataValue parseFetchValueRaw(std::string_view value, std::size_t &cursor,
                                                                 std::string_view item_name) {
            while (cursor < value.size() && std::isspace(static_cast<unsigned char>(value[cursor])) != 0) { ++cursor; }
            if (cursor >= value.size()) { return MessageDataValue{.data = std::string{}}; }

            if (value[cursor] == '(') {
                ++cursor;
                MessageDataList list{};
                while (cursor < value.size()) {
                    while (cursor < value.size() && std::isspace(static_cast<unsigned char>(value[cursor])) != 0) { ++cursor; }
                    if (cursor < value.size() && value[cursor] == ')') {
                        ++cursor;
                        break;
                    }
                    list.push_back(parseFetchValueRaw(value, cursor, {}));
                }
                return MessageDataValue{.data = std::move(list)};
            }

            if (value[cursor] == '"') { return MessageDataValue{.data = parseQuotedString(value, cursor)}; }

            if (value[cursor] == '{') {
                const auto literal_end = value.find('}', cursor);
                if (literal_end == std::string_view::npos) { return MessageDataValue{.data = std::string{}}; }

                const auto literal_size =
                        static_cast<std::size_t>(std::stoul(std::string(value.substr(cursor + 1, literal_end - cursor - 1))));
                cursor = literal_end + 1;
                if (cursor + 1 < value.size() && value[cursor] == '\r' && value[cursor + 1] == '\n') { cursor += 2; }

                const auto literal = std::string(value.substr(cursor, literal_size));
                cursor += literal_size;

                if (upper(item_name).find("HEADER") != std::string::npos) {
                    return MessageDataValue{.data = parseHeaderFields(literal)};
                }
                return MessageDataValue{.data = literal};
            }

            return parseAtomLikeValue(value, cursor);
        }

        [[nodiscard]] inline MessageDataValue parseFetchValue(std::string_view value, std::size_t &cursor,
                                                              std::string_view item_name) {
            auto parsed = parseFetchValueRaw(value, cursor, item_name);
            if ((iequals(item_name, "BODYSTRUCTURE") || iequals(item_name, "BODY")) && parsed.asList() != nullptr) {
                parsed.data = std::make_shared<BodyStructure>(buildBodyStructure(parsed));
            } else if (iequals(item_name, "ENVELOPE") && parsed.asList() != nullptr) {
                auto envelope = parseEnvelope(parsed);
                if (envelope.has_value()) { parsed.data = std::make_shared<Envelope>(std::move(*envelope)); }
            }
            return parsed;
        }

        [[nodiscard]] inline response::FetchData parseFetchData(std::uint32_t sequence_number, std::string_view raw) {
            response::FetchData data{.sequence_number = sequence_number};
            const auto fetch_pos = upper(raw).find("FETCH");
            if (fetch_pos == std::string::npos) { return data; }
            const auto open = raw.find('(', fetch_pos);
            const auto close = raw.rfind(')');
            if (open == std::string::npos || close == std::string::npos || close <= open) { return data; }

            auto inside = std::string_view{raw}.substr(open + 1, close - open - 1);
            std::size_t cursor = 0;
            while (cursor < inside.size()) {
                while (cursor < inside.size()
                       && std::isspace(static_cast<unsigned char>(inside[cursor])) != 0) { ++cursor; }
                if (cursor >= inside.size()) { break; }

                std::string name = parseFetchItemName(inside, cursor);
                while (cursor < inside.size()
                       && std::isspace(static_cast<unsigned char>(inside[cursor])) != 0) { ++cursor; }
                data.items.push_back(MessageDataItem{.name = name, .value = parseFetchValue(inside, cursor, name)});
            }
            return data;
        }

        [[nodiscard]] inline std::expected<response::ServerResponse, Error> parseLine(std::string_view raw) {
            if (raw.empty()) {
                return std::unexpected(
                        Error{.code = Error::CODE::INVALID_SYNTAX, .message = "empty IMAP response line"});
            }

            if (raw.starts_with("+")) {
                std::string text = raw.size() > 1 ? trim(raw.substr(1)) : std::string{};
                return response::ServerResponse{response::Continuation<>{.data = std::move(text)}};
            }

            if (raw.starts_with("*")) {
                auto remainder = trim(raw.substr(1));
                if (remainder.empty()) {
                    return std::unexpected(
                            Error{.code = Error::CODE::INVALID_SYNTAX, .message = "invalid untagged response"});
                }

                if (std::isdigit(static_cast<unsigned char>(remainder.front())) != 0) {
                    const auto space = remainder.find(' ');
                    if (space == std::string::npos) {
                        return std::unexpected(
                                Error{.code = Error::CODE::INVALID_SYNTAX, .message = "invalid numeric response"});
                    }
                    const auto number = static_cast<std::uint32_t>(std::stoul(std::string(remainder.substr(0, space))));
                    remainder = trim(remainder.substr(space + 1));
                    const std::string_view remainder_view{remainder};
                    const auto atom_end = remainder_view.find(' ');
                    const auto atom =
                            atom_end == std::string::npos ? remainder_view : remainder_view.substr(0, atom_end);
                    const auto tail =
                            atom_end == std::string::npos ? std::string_view{} : remainder_view.substr(atom_end + 1);

                    if (iequals(atom, "EXISTS")) {
                        return response::ServerResponse{response::UntaggedServerData{.data = response::Exists{.count = number}}};
                    }
                    if (iequals(atom, "RECENT")) {
                        return response::ServerResponse{response::UntaggedServerData{.data = response::Recent{.count = number}}};
                    }
                    if (iequals(atom, "EXPUNGE")) {
                        return response::ServerResponse{
                                response::UntaggedServerData{.data = response::Expunge{.sequence_number = number}}};
                    }
                    if (iequals(atom, "FETCH")) {
                        return response::ServerResponse{
                                response::UntaggedServerData{.data = parseFetchData(number, std::string(raw))}};
                    }

                    return response::ServerResponse{response::UntaggedServerData{
                            .data = response::GenericData{.name = std::to_string(number) + " " + upper(atom),
                                                          .text = trim(tail)}}};
                }

                const std::string_view remainder_view{remainder};
                const auto atom_end = remainder_view.find(' ');
                const auto atom = atom_end == std::string::npos ? remainder_view : remainder_view.substr(0, atom_end);
                const auto tail = atom_end == std::string::npos ? std::string_view{} : remainder_view.substr(atom_end + 1);

                if (iequals(atom, "OK") || iequals(atom, "NO") || iequals(atom, "BAD") || iequals(atom, "PREAUTH")
                    || iequals(atom, "BYE")) {
                    return response::ServerResponse{
                            response::UntaggedStatus{.data = parseStatusVariant<response::UntaggedStatusData>(
                                                             atom, tail, false)}};
                }

                if (iequals(atom, "CAPABILITY")) {
                    response::CapabilityData data{};
                    auto trimmed = trim(tail);
                    std::size_t cursor = 0;
                    while (cursor < trimmed.size()) {
                        const auto next = trimmed.find(' ', cursor);
                        data.capabilities.push_back(trimmed.substr(cursor, next == std::string::npos ? next : next - cursor));
                        if (next == std::string::npos) { break; }
                        cursor = next + 1;
                        while (cursor < trimmed.size()
                               && std::isspace(static_cast<unsigned char>(trimmed[cursor])) != 0) { ++cursor; }
                    }
                    return response::ServerResponse{response::UntaggedServerData{.data = std::move(data)}};
                }
                if (iequals(atom, "ENABLED")) {
                    response::EnabledData data{};
                    auto trimmed = trim(tail);
                    std::size_t cursor = 0;
                    while (cursor < trimmed.size()) {
                        const auto next = trimmed.find(' ', cursor);
                        data.capabilities.push_back(trimmed.substr(cursor, next == std::string::npos ? next : next - cursor));
                        if (next == std::string::npos) { break; }
                        cursor = next + 1;
                        while (cursor < trimmed.size()
                               && std::isspace(static_cast<unsigned char>(trimmed[cursor])) != 0) { ++cursor; }
                    }
                    return response::ServerResponse{response::UntaggedServerData{.data = std::move(data)}};
                }
                if (iequals(atom, "FLAGS")) {
                    return response::ServerResponse{
                            response::UntaggedServerData{.data = response::FlagsData{.flags = parseParenthesizedAtoms(tail)}}};
                }
                if (iequals(atom, "LIST")) {
                    return response::ServerResponse{
                            response::UntaggedServerData{.data = parseListData(tail)}};
                }
                if (iequals(atom, "STATUS")) {
                    return response::ServerResponse{
                            response::UntaggedServerData{.data = parseStatusData(tail)}};
                }
                if (iequals(atom, "SEARCH") || iequals(atom, "ESEARCH")) {
                    return response::ServerResponse{
                            response::UntaggedServerData{.data = parseSearchData(tail)}};
                }

                return response::ServerResponse{
                        response::UntaggedServerData{.data = response::GenericData{.name = upper(atom), .text = trim(tail)}}};
            }

            const auto space = raw.find(' ');
            if (space == std::string_view::npos) {
                return std::unexpected(
                        Error{.code = Error::CODE::INVALID_SYNTAX, .message = "invalid tagged response"});
            }

            const auto tag = std::string(raw.substr(0, space));
            auto remainder = trim(raw.substr(space + 1));
            const std::string_view remainder_view{remainder};
            const auto atom_end = remainder_view.find(' ');
            const auto atom = atom_end == std::string::npos ? remainder_view : remainder_view.substr(0, atom_end);
            const auto tail = atom_end == std::string::npos ? std::string_view{} : remainder_view.substr(atom_end + 1);

            return response::ServerResponse{
                    response::TaggedStatus{.tag = std::move(tag),
                                           .data = parseStatusVariant<response::TaggedStatusData>(atom, tail, true)}};
        }

    }// namespace detail

    class ResponseParser {
    public:
        void reset() { buffer_.clear(); }

        std::expected<void, Error> feed(std::string_view bytes) {
            buffer_.append(bytes);
            return {};
        }

        std::expected<std::optional<response::ServerResponse>, Error> next() {
            if (buffer_.empty()) { return std::optional<response::ServerResponse>{}; }

            const auto line_end = buffer_.find("\r\n");
            if (line_end == std::string::npos) { return std::optional<response::ServerResponse>{}; }

            const auto first_line = std::string_view{buffer_}.substr(0, line_end);
            auto total_end = line_end + 2;

            if (const auto literal_size = detail::parseLiteralSize(first_line); literal_size.has_value()) {
                const auto literal_begin = line_end + 2;
                const auto after_literal = literal_begin + *literal_size;
                if (buffer_.size() < after_literal) { return std::optional<response::ServerResponse>{}; }
                const auto tail_end = buffer_.find("\r\n", after_literal);
                if (tail_end == std::string::npos) { return std::optional<response::ServerResponse>{}; }
                total_end = tail_end + 2;
            }

            const std::string raw = buffer_.substr(0, total_end);
            buffer_.erase(0, total_end);

            std::string_view line = raw;
            if (raw.size() >= 2 && raw.ends_with("\r\n")) { line.remove_suffix(2); }
            return detail::parseLine(line);
        }

    private:
        std::string buffer_{};
    };

}// namespace usub::unet::mail::imap::wire
