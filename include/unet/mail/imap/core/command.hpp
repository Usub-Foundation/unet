#pragma once

#include <cstdint>

namespace usub::unet::mail::imap::core {

    enum class COMMAND : std::uint8_t {
        CAPABILITY,
        NOOP,
        LOGOUT,
        STARTTLS,
        AUTHENTICATE,
        LOGIN,
        ENABLE,
        SELECT,
        EXAMINE,
        CREATE,
        DELETE,
        RENAME,
        SUBSCRIBE,
        UNSUBSCRIBE,
        LIST,
        NAMESPACE,
        STATUS,
        APPEND,
        IDLE,
        CLOSE,
        UNSELECT,
        EXPUNGE,
        SEARCH,
        FETCH,
        STORE,
        COPY,
        MOVE,
        UID,
    };

}// namespace usub::unet::mail::imap::core
