#pragma once

#include <string_view>

#include <uvent/Uvent.h>

namespace usub::unet::core {
    template<class>
    inline constexpr bool always_false_v = false;

    class Config;

    template<class StreamHandler>
    class Acceptor {
    public:
        Acceptor() = default;
        ~Acceptor() = default;

        template<class OnConnection>
        usub::uvent::task::Awaitable<void> acceptLoop(OnConnection, Config &, std::string_view /*prefix*/) {
            static_assert(always_false_v<StreamHandler>, "Acceptor not implemented for this stream type");
            co_return;
        }
    };
}// namespace usub::unet::core
