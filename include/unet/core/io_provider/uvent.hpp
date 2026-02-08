#pragma once

#include <uvent/Uvent.h>

namespace usub::unet {
    template<typename T>
    using Awaitable = usub::uvent::task::Awaitable<T>;


}