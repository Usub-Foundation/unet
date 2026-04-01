#pragma once

#include <cctype>
#include <expected>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "unet/mail/imap/core/command.hpp"
#include "unet/mail/imap/core/error.hpp"

namespace usub::unet::mail::imap::wire {
    namespace detail {

        [[nodiscard]] inline bool validTag(std::string_view tag) noexcept {
            if (tag.empty()) { return false; }
            for (const unsigned char ch: tag) {
                if (std::isalnum(ch) == 0) { return false; }
            }
            return true;
        }

        [[nodiscard]] inline std::string escapeQuoted(std::string_view value) {
            std::string out;
            out.reserve(value.size() + 2);
            out.push_back('"');
            for (const char ch: value) {
                if (ch == '\\' || ch == '"') { out.push_back('\\'); }
                out.push_back(ch);
            }
            out.push_back('"');
            return out;
        }

        [[nodiscard]] inline std::string serializeSequenceSetValue(const SequenceSetValue &value) {
            if (std::holds_alternative<std::monostate>(value)) { return "*"; }
            return std::to_string(std::get<std::uint32_t>(value));
        }

        [[nodiscard]] inline std::string serializeSequenceSet(const SequenceSet &value) {
            std::string out;
            for (std::size_t i = 0; i < value.size(); ++i) {
                if (i != 0) { out.push_back(','); }
                out.append(serializeSequenceSetValue(value[i].first));
                if (value[i].second.has_value()) {
                    out.push_back(':');
                    out.append(serializeSequenceSetValue(*value[i].second));
                }
            }
            return out;
        }

        [[nodiscard]] inline std::string serializeAtomList(const std::vector<std::string> &values) {
            std::string out{"("};
            for (std::size_t i = 0; i < values.size(); ++i) {
                if (i != 0) { out.push_back(' '); }
                out.append(values[i]);
            }
            out.push_back(')');
            return out;
        }

        [[nodiscard]] inline std::string serializeQuotedList(const std::vector<std::string> &values) {
            std::string out{"("};
            for (std::size_t i = 0; i < values.size(); ++i) {
                if (i != 0) { out.push_back(' '); }
                out.append(escapeQuoted(values[i]));
            }
            out.push_back(')');
            return out;
        }

        [[nodiscard]] inline std::string serializeSearchKey(const command::SearchKey &key);

        [[nodiscard]] inline std::string serializeSearchKey(const command::SearchKey &key) {
            return std::visit(
                    [](const auto &data) -> std::string {
                        using T = std::decay_t<decltype(data)>;

                        if constexpr (std::is_same_v<T, command::SearchKey::All>) {
                            return "ALL";
                        } else if constexpr (std::is_same_v<T, command::SearchKey::Answered>) {
                            return "ANSWERED";
                        } else if constexpr (std::is_same_v<T, command::SearchKey::Deleted>) {
                            return "DELETED";
                        } else if constexpr (std::is_same_v<T, command::SearchKey::Draft>) {
                            return "DRAFT";
                        } else if constexpr (std::is_same_v<T, command::SearchKey::Flagged>) {
                            return "FLAGGED";
                        } else if constexpr (std::is_same_v<T, command::SearchKey::New>) {
                            return "NEW";
                        } else if constexpr (std::is_same_v<T, command::SearchKey::Old>) {
                            return "OLD";
                        } else if constexpr (std::is_same_v<T, command::SearchKey::Recent>) {
                            return "RECENT";
                        } else if constexpr (std::is_same_v<T, command::SearchKey::Seen>) {
                            return "SEEN";
                        } else if constexpr (std::is_same_v<T, command::SearchKey::Unanswered>) {
                            return "UNANSWERED";
                        } else if constexpr (std::is_same_v<T, command::SearchKey::Undeleted>) {
                            return "UNDELETED";
                        } else if constexpr (std::is_same_v<T, command::SearchKey::Undraft>) {
                            return "UNDRAFT";
                        } else if constexpr (std::is_same_v<T, command::SearchKey::Unflagged>) {
                            return "UNFLAGGED";
                        } else if constexpr (std::is_same_v<T, command::SearchKey::Unseen>) {
                            return "UNSEEN";
                        } else if constexpr (std::is_same_v<T, command::SearchKey::Bcc>) {
                            return "BCC " + escapeQuoted(data.value);
                        } else if constexpr (std::is_same_v<T, command::SearchKey::Before>) {
                            return "BEFORE " + data.value;
                        } else if constexpr (std::is_same_v<T, command::SearchKey::Body>) {
                            return "BODY " + escapeQuoted(data.value);
                        } else if constexpr (std::is_same_v<T, command::SearchKey::Cc>) {
                            return "CC " + escapeQuoted(data.value);
                        } else if constexpr (std::is_same_v<T, command::SearchKey::From>) {
                            return "FROM " + escapeQuoted(data.value);
                        } else if constexpr (std::is_same_v<T, command::SearchKey::Keyword>) {
                            return "KEYWORD " + data.value;
                        } else if constexpr (std::is_same_v<T, command::SearchKey::Larger>) {
                            return "LARGER " + std::to_string(data.value);
                        } else if constexpr (std::is_same_v<T, command::SearchKey::On>) {
                            return "ON " + data.value;
                        } else if constexpr (std::is_same_v<T, command::SearchKey::SentBefore>) {
                            return "SENTBEFORE " + data.value;
                        } else if constexpr (std::is_same_v<T, command::SearchKey::SentOn>) {
                            return "SENTON " + data.value;
                        } else if constexpr (std::is_same_v<T, command::SearchKey::SentSince>) {
                            return "SENTSINCE " + data.value;
                        } else if constexpr (std::is_same_v<T, command::SearchKey::Since>) {
                            return "SINCE " + data.value;
                        } else if constexpr (std::is_same_v<T, command::SearchKey::Smaller>) {
                            return "SMALLER " + std::to_string(data.value);
                        } else if constexpr (std::is_same_v<T, command::SearchKey::Subject>) {
                            return "SUBJECT " + escapeQuoted(data.value);
                        } else if constexpr (std::is_same_v<T, command::SearchKey::Text>) {
                            return "TEXT " + escapeQuoted(data.value);
                        } else if constexpr (std::is_same_v<T, command::SearchKey::To>) {
                            return "TO " + escapeQuoted(data.value);
                        } else if constexpr (std::is_same_v<T, command::SearchKey::Uid>) {
                            return "UID " + serializeSequenceSet(data.value);
                        } else if constexpr (std::is_same_v<T, command::SearchKey::Header>) {
                            return "HEADER " + escapeQuoted(data.field_name) + " " + escapeQuoted(data.value);
                        } else if constexpr (std::is_same_v<T, command::SearchKey::Not>) {
                            return "NOT " + serializeSearchKey(*data.key);
                        } else if constexpr (std::is_same_v<T, command::SearchKey::Or>) {
                            return "OR " + serializeSearchKey(*data.left) + " " + serializeSearchKey(*data.right);
                        } else if constexpr (std::is_same_v<T, command::SearchKey::Group>) {
                            std::string out{"("};
                            for (std::size_t i = 0; i < data.keys.size(); ++i) {
                                if (i != 0) { out.push_back(' '); }
                                out.append(serializeSearchKey(*data.keys[i]));
                            }
                            out.push_back(')');
                            return out;
                        } else {
                            return {};
                        }
                    },
                    key.data);
        }

        template<typename CommandData>
        [[nodiscard]] inline std::string commandName();

        template<>
        [[nodiscard]] inline std::string commandName<command::Capability>() {
            return "CAPABILITY";
        }
        template<>
        [[nodiscard]] inline std::string commandName<command::Noop>() {
            return "NOOP";
        }
        template<>
        [[nodiscard]] inline std::string commandName<command::Logout>() {
            return "LOGOUT";
        }
        template<>
        [[nodiscard]] inline std::string commandName<command::StartTls>() {
            return "STARTTLS";
        }
        template<>
        [[nodiscard]] inline std::string commandName<command::Authenticate>() {
            return "AUTHENTICATE";
        }
        template<>
        [[nodiscard]] inline std::string commandName<command::Login>() {
            return "LOGIN";
        }
        template<>
        [[nodiscard]] inline std::string commandName<command::Enable>() {
            return "ENABLE";
        }
        template<>
        [[nodiscard]] inline std::string commandName<command::Select>() {
            return "SELECT";
        }
        template<>
        [[nodiscard]] inline std::string commandName<command::Examine>() {
            return "EXAMINE";
        }
        template<>
        [[nodiscard]] inline std::string commandName<command::Create>() {
            return "CREATE";
        }
        template<>
        [[nodiscard]] inline std::string commandName<command::Delete>() {
            return "DELETE";
        }
        template<>
        [[nodiscard]] inline std::string commandName<command::Rename>() {
            return "RENAME";
        }
        template<>
        [[nodiscard]] inline std::string commandName<command::Subscribe>() {
            return "SUBSCRIBE";
        }
        template<>
        [[nodiscard]] inline std::string commandName<command::Unsubscribe>() {
            return "UNSUBSCRIBE";
        }
        template<>
        [[nodiscard]] inline std::string commandName<command::List>() {
            return "LIST";
        }
        template<>
        [[nodiscard]] inline std::string commandName<command::Namespace>() {
            return "NAMESPACE";
        }
        template<>
        [[nodiscard]] inline std::string commandName<command::Status>() {
            return "STATUS";
        }
        template<>
        [[nodiscard]] inline std::string commandName<command::Append>() {
            return "APPEND";
        }
        template<>
        [[nodiscard]] inline std::string commandName<command::Idle>() {
            return "IDLE";
        }
        template<>
        [[nodiscard]] inline std::string commandName<command::Check>() {
            return "CHECK";
        }
        template<>
        [[nodiscard]] inline std::string commandName<command::Close>() {
            return "CLOSE";
        }
        template<>
        [[nodiscard]] inline std::string commandName<command::Unselect>() {
            return "UNSELECT";
        }
        template<>
        [[nodiscard]] inline std::string commandName<command::Expunge>() {
            return "EXPUNGE";
        }
        template<>
        [[nodiscard]] inline std::string commandName<command::Search>() {
            return "SEARCH";
        }
        template<>
        [[nodiscard]] inline std::string commandName<command::Fetch>() {
            return "FETCH";
        }
        template<>
        [[nodiscard]] inline std::string commandName<command::Store>() {
            return "STORE";
        }
        template<>
        [[nodiscard]] inline std::string commandName<command::Copy>() {
            return "COPY";
        }
        template<>
        [[nodiscard]] inline std::string commandName<command::Move>() {
            return "MOVE";
        }

        template<typename CommandData>
        [[nodiscard]] inline std::string serializeArguments(const CommandData &);

        template<>
        [[nodiscard]] inline std::string serializeArguments(const command::Capability &) {
            return {};
        }
        template<>
        [[nodiscard]] inline std::string serializeArguments(const command::Noop &) {
            return {};
        }
        template<>
        [[nodiscard]] inline std::string serializeArguments(const command::Logout &) {
            return {};
        }
        template<>
        [[nodiscard]] inline std::string serializeArguments(const command::StartTls &) {
            return {};
        }
        template<>
        [[nodiscard]] inline std::string serializeArguments(const command::Namespace &) {
            return {};
        }
        template<>
        [[nodiscard]] inline std::string serializeArguments(const command::Idle &) {
            return {};
        }
        template<>
        [[nodiscard]] inline std::string serializeArguments(const command::Check &) {
            return {};
        }
        template<>
        [[nodiscard]] inline std::string serializeArguments(const command::Close &) {
            return {};
        }
        template<>
        [[nodiscard]] inline std::string serializeArguments(const command::Unselect &) {
            return {};
        }
        template<>
        [[nodiscard]] inline std::string serializeArguments(const command::Expunge &) {
            return {};
        }

        template<>
        [[nodiscard]] inline std::string serializeArguments(const command::Authenticate &value) {
            if (value.initial_response.has_value()) {
                return value.sasl_auth_name + " " + *value.initial_response;
            }
            return value.sasl_auth_name;
        }

        template<>
        [[nodiscard]] inline std::string serializeArguments(const command::Login &value) {
            return escapeQuoted(value.username) + " " + escapeQuoted(value.password);
        }

        template<>
        [[nodiscard]] inline std::string serializeArguments(const command::Enable &value) {
            std::string out;
            for (std::size_t i = 0; i < value.capability_names.size(); ++i) {
                if (i != 0) { out.push_back(' '); }
                out.append(value.capability_names[i]);
            }
            return out;
        }

        template<>
        [[nodiscard]] inline std::string serializeArguments(const command::Select &value) {
            return escapeQuoted(value.mailbox_name);
        }

        template<>
        [[nodiscard]] inline std::string serializeArguments(const command::Examine &value) {
            return escapeQuoted(value.mailbox_name);
        }

        template<>
        [[nodiscard]] inline std::string serializeArguments(const command::Create &value) {
            return escapeQuoted(value.mailbox_name);
        }

        template<>
        [[nodiscard]] inline std::string serializeArguments(const command::Delete &value) {
            return escapeQuoted(value.mailbox_name);
        }

        template<>
        [[nodiscard]] inline std::string serializeArguments(const command::Subscribe &value) {
            return escapeQuoted(value.mailbox_name);
        }

        template<>
        [[nodiscard]] inline std::string serializeArguments(const command::Unsubscribe &value) {
            return escapeQuoted(value.mailbox_name);
        }

        template<>
        [[nodiscard]] inline std::string serializeArguments(const command::Rename &value) {
            return escapeQuoted(value.existing_mailbox_name) + " " + escapeQuoted(value.new_mailbox_name);
        }

        template<>
        [[nodiscard]] inline std::string serializeArguments(const command::List &value) {
            return std::visit(
                    [](const auto &data) {
                        using T = std::decay_t<decltype(data)>;
                        if constexpr (std::is_same_v<T, command::ListBasic>) {
                            return escapeQuoted(data.reference_name) + " " + escapeQuoted(data.mailbox_name);
                        } else {
                            std::string out;
                            if (data.selection_options.has_value()) {
                                out.append(serializeAtomList(*data.selection_options));
                                out.push_back(' ');
                            }
                            out.append(escapeQuoted(data.reference_name));
                            out.push_back(' ');
                            if (data.mailbox_patterns.size() == 1) {
                                out.append(escapeQuoted(data.mailbox_patterns.front()));
                            } else {
                                out.append(serializeQuotedList(data.mailbox_patterns));
                            }
                            if (data.return_options.has_value()) {
                                out.append(" RETURN ");
                                out.append(serializeAtomList(*data.return_options));
                            }
                            return out;
                        }
                    },
                    value);
        }

        template<>
        [[nodiscard]] inline std::string serializeArguments(const command::Status &value) {
            return escapeQuoted(value.mailbox_name) + " " + serializeAtomList(value.status_data_item_names);
        }

        template<>
        [[nodiscard]] inline std::string serializeArguments(const command::Append &value) {
            std::string out = escapeQuoted(value.mailbox_name);
            if (value.flag_list.has_value()) {
                out.push_back(' ');
                out.append(serializeAtomList(*value.flag_list));
            }
            if (value.date_time.has_value()) {
                out.push_back(' ');
                out.append(escapeQuoted(*value.date_time));
            }
            out.push_back(' ');
            out.push_back('{');
            out.append(std::to_string(value.message.size()));
            out.append("}\r\n");
            out.append(value.message);
            return out;
        }

        template<>
        [[nodiscard]] inline std::string serializeArguments(const command::Search &value) {
            std::string out;
            if (value.result_specifier.has_value()) {
                out.append("RETURN ");
                out.append(serializeAtomList(value.result_specifier->return_options));
            }
            if (value.charset.has_value()) {
                if (!out.empty()) { out.push_back(' '); }
                out.append("CHARSET ");
                out.append(*value.charset);
            }
            for (const auto &criterion: value.search_criteria) {
                if (!out.empty()) { out.push_back(' '); }
                out.append(serializeSearchKey(criterion));
            }
            return out;
        }

        template<>
        [[nodiscard]] inline std::string serializeArguments(const command::Fetch &value) {
            return serializeSequenceSet(value.sequence_set) + " " + serializeAtomList(value.data_item_names);
        }

        template<>
        [[nodiscard]] inline std::string serializeArguments(const command::Store &value) {
            std::string mode = "FLAGS";
            switch (value.mode) {
                case command::Store::MODE::REPLACE:
                    mode = "FLAGS";
                    break;
                case command::Store::MODE::ADD:
                    mode = "+FLAGS";
                    break;
                case command::Store::MODE::REMOVE:
                    mode = "-FLAGS";
                    break;
            }
            if (value.silent) { mode.append(".SILENT"); }
            return serializeSequenceSet(value.sequence_set) + " " + mode + " " + serializeAtomList(value.flag_list);
        }

        template<>
        [[nodiscard]] inline std::string serializeArguments(const command::Copy &value) {
            return serializeSequenceSet(value.sequence_set) + " " + escapeQuoted(value.mailbox_name);
        }

        template<>
        [[nodiscard]] inline std::string serializeArguments(const command::Move &value) {
            return serializeSequenceSet(value.sequence_set) + " " + escapeQuoted(value.mailbox_name);
        }

    }// namespace detail

    class CommandSerializer {
    public:
        template<typename CommandData>
        static std::expected<std::string, Error> serialize(const command::Command<CommandData> &request) {
            if (!detail::validTag(request.tag)) {
                return std::unexpected(
                        Error{.code = Error::CODE::INVALID_TOKEN, .message = "invalid IMAP command tag"});
            }

            std::string out = request.tag;
            out.push_back(' ');
            out.append(detail::commandName<CommandData>());

            const auto arguments = detail::serializeArguments(request.data);
            if (!arguments.empty()) {
                out.push_back(' ');
                out.append(arguments);
            }

            out.append("\r\n");
            return out;
        }
    };

}// namespace usub::unet::mail::imap::wire
