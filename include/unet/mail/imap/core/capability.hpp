#pragma once

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "unet/mail/imap/core/error.hpp"
#include "unet/mail/imap/core/response.hpp"

namespace usub::unet::mail::imap::core {

    struct CapabilitySet {
        std::vector<std::string> values{};

        [[nodiscard]] bool has(std::string_view token) const noexcept;
        [[nodiscard]] std::optional<std::string> firstAuthMechanism() const;
    };

    std::expected<CapabilitySet, Error> parseCapabilities(const Response &response);

}// namespace usub::unet::mail::imap::core
