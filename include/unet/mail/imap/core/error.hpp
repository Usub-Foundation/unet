#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace usub::unet::mail::imap::core {

    enum class ErrorCode : std::uint8_t {
        InvalidInput,
        InvalidToken,
        InvalidSyntax,
        InvalidState,
        LimitExceeded,
        Unsupported,
        Io,
    };

    struct Error {
        ErrorCode code{ErrorCode::InvalidInput};
        std::string message{};
        std::size_t offset{0};
    };

}// namespace usub::unet::mail::imap::core
