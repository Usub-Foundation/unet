#pragma once

#include <expected>
#include <span>
#include <string>
#include <vector>

#include "unet/mail/imap/core/error.hpp"

namespace usub::unet::mail::imap::core {

    struct SearchKey {
        std::string name{};
        std::vector<std::string> arguments{};
    };

    std::expected<std::string, Error> serializeSearchKeys(std::span<const SearchKey> keys);

}// namespace usub::unet::mail::imap::core
