#include "unet/mail/imap/core/sequence_set.hpp"

#include <charconv>
#include <limits>

namespace usub::unet::mail::imap::core {
    namespace {

        [[nodiscard]] std::expected<SequenceNumber, Error> parseSequenceNumber(std::string_view token,
                                                                                std::size_t offset) {
            if (token == "*") { return SequenceNumber{.kind = SequenceNumber::Kind::Asterisk, .value = 0}; }

            if (token.empty()) {
                return std::unexpected(
                        Error{.code = ErrorCode::InvalidSyntax, .message = "empty sequence number", .offset = offset});
            }

            std::uint64_t number = 0;
            const char *begin = token.data();
            const char *end = begin + token.size();
            auto [ptr, ec] = std::from_chars(begin, end, number);
            if (ec != std::errc() || ptr != end || number == 0 || number > std::numeric_limits<std::uint32_t>::max()) {
                return std::unexpected(Error{.code = ErrorCode::InvalidToken,
                                             .message = "invalid sequence number token",
                                             .offset = offset});
            }

            return SequenceNumber{.kind = SequenceNumber::Kind::Value, .value = static_cast<std::uint32_t>(number)};
        }

    }// namespace

    bool SequenceNumber::valid() const noexcept { return kind == Kind::Asterisk || value != 0; }

    std::expected<SequenceSet, Error> SequenceSet::parse(std::string_view input) {
        if (input.empty()) {
            return std::unexpected(
                    Error{.code = ErrorCode::InvalidInput, .message = "sequence-set must not be empty", .offset = 0});
        }

        SequenceSet set{};

        std::size_t cursor = 0;
        while (cursor < input.size()) {
            const std::size_t comma = input.find(',', cursor);
            const std::size_t end = comma == std::string_view::npos ? input.size() : comma;
            const std::string_view item = input.substr(cursor, end - cursor);

            if (item.empty()) {
                return std::unexpected(
                        Error{.code = ErrorCode::InvalidSyntax, .message = "empty sequence-set item", .offset = cursor});
            }

            if (item.find(' ') != std::string_view::npos || item.find('\t') != std::string_view::npos) {
                return std::unexpected(Error{.code = ErrorCode::InvalidSyntax,
                                             .message = "sequence-set does not allow spaces",
                                             .offset = cursor});
            }

            const std::size_t colon = item.find(':');
            if (colon == std::string_view::npos) {
                auto single = parseSequenceNumber(item, cursor);
                if (!single) { return std::unexpected(single.error()); }
                set.ranges_.push_back(SequenceRange{.first = *single, .last = *single});
            } else {
                if (item.find(':', colon + 1) != std::string_view::npos) {
                    return std::unexpected(Error{.code = ErrorCode::InvalidSyntax,
                                                 .message = "sequence range contains too many separators",
                                                 .offset = cursor + colon + 1});
                }

                auto first = parseSequenceNumber(item.substr(0, colon), cursor);
                if (!first) { return std::unexpected(first.error()); }

                auto last = parseSequenceNumber(item.substr(colon + 1), cursor + colon + 1);
                if (!last) { return std::unexpected(last.error()); }

                set.ranges_.push_back(SequenceRange{.first = *first, .last = *last});
            }

            if (comma == std::string_view::npos) { break; }
            cursor = comma + 1;
        }

        return set;
    }

    const std::vector<SequenceRange> &SequenceSet::ranges() const noexcept { return ranges_; }

    std::string SequenceSet::toString() const {
        std::string out;
        bool first_item = true;

        auto append_number = [&out](const SequenceNumber &number) {
            if (number.kind == SequenceNumber::Kind::Asterisk) {
                out.push_back('*');
            } else {
                out.append(std::to_string(number.value));
            }
        };

        for (const auto &range: ranges_) {
            if (!first_item) { out.push_back(','); }
            first_item = false;

            append_number(range.first);
            if (!(range.first.kind == range.last.kind && range.first.value == range.last.value)) {
                out.push_back(':');
                append_number(range.last);
            }
        }

        return out;
    }

}// namespace usub::unet::mail::imap::core
