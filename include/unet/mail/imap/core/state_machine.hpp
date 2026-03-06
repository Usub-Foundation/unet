#pragma once

#include <cstdint>
#include <expected>

#include "unet/mail/imap/core/command.hpp"
#include "unet/mail/imap/core/error.hpp"
#include "unet/mail/imap/core/response.hpp"

namespace usub::unet::mail::imap::core {

    enum class SessionState : std::uint8_t {
        Disconnected,
        NotAuthenticated,
        Authenticated,
        Selected,
        Logout,
    };

    class StateMachine {
    public:
        [[nodiscard]] SessionState state() const noexcept;

        void reset() noexcept;

        std::expected<void, Error> onGreeting(const Response &response);
        std::expected<void, Error> onTaggedResponse(COMMAND command, const TaggedResponse &response);
        std::expected<void, Error> onUntaggedResponse(const UntaggedResponse &response);

        [[nodiscard]] bool canSend(COMMAND command) const noexcept;

    private:
        SessionState state_{SessionState::Disconnected};
    };

}// namespace usub::unet::mail::imap::core
