#include "unet/mail/imap/core/capability.hpp"

#include <algorithm>
#include <cctype>

namespace usub::unet::mail::imap::core {
    namespace {

        [[nodiscard]] bool iequals(std::string_view lhs, std::string_view rhs) noexcept {
            if (lhs.size() != rhs.size()) { return false; }
            for (std::size_t i = 0; i < lhs.size(); ++i) {
                unsigned char a = static_cast<unsigned char>(lhs[i]);
                unsigned char b = static_cast<unsigned char>(rhs[i]);
                if (std::toupper(a) != std::toupper(b)) { return false; }
            }
            return true;
        }

        [[nodiscard]] std::vector<std::string> splitTokens(std::string_view input) {
            std::vector<std::string> tokens;
            std::size_t i = 0;
            while (i < input.size()) {
                while (i < input.size() && input[i] == ' ') { ++i; }
                if (i >= input.size()) { break; }

                const std::size_t begin = i;
                while (i < input.size() && input[i] != ' ') { ++i; }
                tokens.emplace_back(input.substr(begin, i - begin));
            }
            return tokens;
        }

        [[nodiscard]] bool startsWithCaseInsensitive(std::string_view value, std::string_view prefix) {
            if (value.size() < prefix.size()) { return false; }
            return iequals(value.substr(0, prefix.size()), prefix);
        }

    }// namespace

    bool CapabilitySet::has(std::string_view token) const noexcept {
        return std::any_of(values.begin(), values.end(), [token](const std::string &existing) {
            return iequals(existing, token);
        });
    }

    std::optional<std::string> CapabilitySet::firstAuthMechanism() const {
        for (const auto &token: values) {
            if (startsWithCaseInsensitive(token, "AUTH=") && token.size() > 5) {
                return token.substr(5);
            }
        }
        return std::nullopt;
    }

    std::expected<CapabilitySet, Error> parseCapabilities(const Response &response) {
        CapabilitySet set{};

        auto parse_from_text = [&set](std::string_view text) -> std::expected<void, Error> {
            auto tokens = splitTokens(text);
            if (tokens.empty()) {
                return std::unexpected(
                        Error{.code = ErrorCode::InvalidSyntax, .message = "CAPABILITY payload is empty"});
            }
            set.values = std::move(tokens);
            return {};
        };

        if (response.kind != Response::Kind::Untagged) {
            return std::unexpected(
                    Error{.code = ErrorCode::InvalidInput, .message = "response is not an untagged CAPABILITY"});
        }

        const auto &untagged = std::get<UntaggedResponse>(response.data);
        if (const auto *data = std::get_if<UntaggedDataResponse>(&untagged.payload)) {
            if (!iequals(data->atom.value, "CAPABILITY")) {
                return std::unexpected(
                        Error{.code = ErrorCode::InvalidInput, .message = "response atom is not CAPABILITY"});
            }
            auto parsed = parse_from_text(data->text);
            if (!parsed) { return std::unexpected(parsed.error()); }
            return set;
        }

        if (const auto *status = std::get_if<UntaggedStatusResponse>(&untagged.payload)) {
            if (!status->status.code.has_value()) {
                return std::unexpected(
                        Error{.code = ErrorCode::InvalidInput, .message = "status response has no response code"});
            }
            if (!iequals(status->status.code->name.value, "CAPABILITY")) {
                return std::unexpected(
                        Error{.code = ErrorCode::InvalidInput, .message = "status response code is not CAPABILITY"});
            }
            auto parsed = parse_from_text(status->status.code->text);
            if (!parsed) { return std::unexpected(parsed.error()); }
            return set;
        }

        return std::unexpected(Error{.code = ErrorCode::InvalidInput,
                                     .message = "response payload is not CAPABILITY-compatible"});
    }

}// namespace usub::unet::mail::imap::core
