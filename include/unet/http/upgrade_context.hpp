#pragma once

#include <functional>

#include "unet/http/session.hpp"

namespace usub::unet::http {

    // Passed by ref to upgrade route handlers. The handler calls ctx.accept() to signal
    // a protocol upgrade, or leaves it untouched to fall back to normal HTTP (Response& is sent).
    struct UpgradeContext {
        bool accepted{false};
        std::move_only_function<SessionBox()> make_session;

        // Signal upgrade. Server will send the handler's Response& as the wire handshake,
        // then transition the connection to the session produced by factory().
        void accept(std::move_only_function<SessionBox()> factory) {
            accepted   = true;
            make_session = std::move(factory);
        }
    };

}  // namespace usub::unet::http
