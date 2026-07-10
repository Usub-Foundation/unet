#pragma once

#include <uvent/Uvent.h>
#include <uvent/sync/AsyncChannel.h>
#include <uvent/tasks/Awaitable.h>

namespace usub::unet {
    template<typename T>
    using Awaitable = usub::uvent::task::Awaitable<T>;

    using Buffer = usub::uvent::utils::DynamicBuffer;

    template<typename... Ts>
    using AsyncChannel = usub::uvent::sync::AsyncChannel<Ts...>;
}// namespace usub::unet