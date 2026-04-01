#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace usub::unet::mail::imap {

    struct Error {
        enum class CODE : std::uint8_t {
            INVALID_INPUT,
            INVALID_TOKEN,
            INVALID_SYNTAX,
            INVALID_STATE,
            LIMIT_EXCEEDED,
            UNSUPPORTED,
            IO,
        };

        CODE code{CODE::INVALID_INPUT};
        std::string message{};
        std::size_t offset{0};
    };

}// namespace usub::unet::mail::imap
