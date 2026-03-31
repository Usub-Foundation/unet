#include "unet/mail/imap/core/response.hpp"

#include <cctype>

namespace usub::unet::mail::imap::core {
    namespace {

        [[nodiscard]] bool equalsAsciiInsensitive(std::string_view lhs, std::string_view rhs) noexcept {
            if (lhs.size() != rhs.size()) { return false; }

            for (std::size_t i = 0; i < lhs.size(); ++i) {
                const auto left = static_cast<unsigned char>(lhs[i]);
                const auto right = static_cast<unsigned char>(rhs[i]);
                if (std::tolower(left) != std::tolower(right)) { return false; }
            }

            return true;
        }

    }// namespace

    bool isOk(const StatusInfo &status) noexcept { return status.condition == ResponseCondition::OK; }

    std::optional<std::uint32_t> extractExists(const Response &response) noexcept {
        if (response.kind != Response::Kind::Untagged) { return std::nullopt; }

        const auto &untagged = std::get<UntaggedResponse>(response.data);
        const auto *numeric = std::get_if<UntaggedNumericResponse>(&untagged.payload);
        if (!numeric) { return std::nullopt; }
        if (!equalsAsciiInsensitive(numeric->atom.value, "EXISTS")) { return std::nullopt; }
        return numeric->number;
    }

    std::optional<std::string_view> extractFetchLiteral(const Response &response) noexcept {
        if (response.kind != Response::Kind::Untagged) { return std::nullopt; }

        const auto &untagged = std::get<UntaggedResponse>(response.data);
        const auto *numeric = std::get_if<UntaggedNumericResponse>(&untagged.payload);
        if (!numeric) { return std::nullopt; }
        if (!equalsAsciiInsensitive(numeric->atom.value, "FETCH")) { return std::nullopt; }
        if (!numeric->literal.has_value()) { return std::nullopt; }
        return std::string_view{numeric->literal->value};
    }

}// namespace usub::unet::mail::imap::core
