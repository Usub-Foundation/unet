#pragma once

#include <functional>

#include "unet/http/session.hpp"

namespace usub::unet::http {
    #if defined(__cpp_lib_move_only_function) && __cpp_lib_move_only_function >= 202110L
    template<typename Signature>
    using upgrade_session_factory = std::move_only_function<Signature>;
    #else
    template<typename Signature>
    using upgrade_session_factory = std::function<Signature>;
    #endif

    // Passed by ref to upgrade route handlers. The handler calls ctx.accept() to signal
    // a protocol upgrade, or leaves it untouched to fall back to normal HTTP (Response& is sent).
    struct UpgradeContext {
        bool accepted{false};
        upgrade_session_factory<SessionBox()> make_session;

        // Signal upgrade. Server will send the handler's Response& as the wire handshake,
        // then transition the connection to the session produced by factory().
        void accept(upgrade_session_factory<SessionBox()> factory) {
            accepted   = true;
            make_session = std::move(factory);
        }
    };

}  // namespace usub::unet::http
