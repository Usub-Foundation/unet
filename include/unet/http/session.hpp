#pragma once

#include <any>
#include <memory>
#include <string_view>

#include "unet/core/streams/stream.hpp"

namespace usub::unet::http {

    template<enum VERSION, typename RouterType>
    class ServerSession;

    template<enum VERSION>
    class ClientSession;

    // Distinguishes normal HTTP routes from upgrade routes in the router.
    enum class RouteKind : uint8_t { Normal, Upgrade };

    struct SessionAction {
        enum class Kind : uint8_t { Continue, Upgrade, Close, Error };
        Kind        kind{Kind::Continue};
        std::string out_bytes;
        std::string carry_bytes;
        // Holds a SessionBox (via std::any) when kind == Upgrade.
        // Replaces upgrade_proto + upgrade_data — no string matching, no any_cast for protocol.
        std::any    next_session_box;
    };

    struct SessionOps {
        using OnBytes = usub::uvent::task::Awaitable<SessionAction> (*)(std::any &, std::string_view,
                                                                        usub::unet::core::stream::Transport &);
        using OnClose = usub::uvent::task::Awaitable<void> (*)(std::any &);
        OnBytes on_bytes{};
        OnClose on_close{};
    };

    struct SessionBox {
        std::any state;
        SessionOps ops;

        template<class T, class... Args>
        static SessionBox make(Args &&...args) {
            SessionBox b;
            b.state.emplace<T>(std::forward<Args>(args)...);

            b.ops.on_bytes =
                    +[](std::any &a, std::string_view data,
                        usub::unet::core::stream::Transport &tr) -> usub::uvent::task::Awaitable<SessionAction> {
                auto *p = std::any_cast<T>(&a);
                if (!p) co_return SessionAction{.kind = SessionAction::Kind::Error};
                co_return co_await p->onBytes(data, tr);
            };

            b.ops.on_close = +[](std::any &a) -> usub::uvent::task::Awaitable<void> {
                if (auto *p = std::any_cast<T>(&a)) co_await p->onClose();
                co_return;
            };

            return b;
        }
    };
}// namespace usub::unet::http
