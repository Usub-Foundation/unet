#pragma once

#include <functional>
#include <memory>
#include <utility>

#include "unet/core/io_provider.hpp"
#include "unet/core/transport/transport.hpp"

namespace usub::unet::http {

#if defined(__cpp_lib_move_only_function) && __cpp_lib_move_only_function >= 202110L
    template<typename Signature>
    using upgrade_action = std::move_only_function<Signature>;
#else
    template<typename Signature>
    using upgrade_action = std::function<Signature>;
#endif

    struct UpgradeContext {
        bool accepted{false};
        upgrade_action<usub::uvent::task::Awaitable<void>(std::unique_ptr<core::transport::Transport>, Buffer)> spawn;

        void
        accept(upgrade_action<usub::uvent::task::Awaitable<void>(std::unique_ptr<core::transport::Transport>, Buffer)>
                       action) {
            accepted = true;
            spawn = std::move(action);
        }
    };

}// namespace usub::unet::http
