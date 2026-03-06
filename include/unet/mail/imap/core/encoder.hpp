#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "unet/mail/imap/core/command.hpp"
#include "unet/mail/imap/core/data_format.hpp"
#include "unet/mail/imap/core/error.hpp"

namespace usub::unet::mail::imap::core {

    struct CommandRequest {
        std::string tag{};
        COMMAND command{COMMAND::NOOP};
        std::vector<Value> arguments{};
    };

    [[nodiscard]] std::string_view commandToString(COMMAND command) noexcept;

    class CommandEncoder {
    public:
        static std::expected<std::string, Error> encode(const CommandRequest &request);

        static std::expected<std::string, Error> encodeSimple(std::string_view tag, COMMAND command);

        static std::expected<std::string, Error>
        encodeLogin(std::string_view tag, std::string_view username, std::string_view password);
    };

}// namespace usub::unet::mail::imap::core
