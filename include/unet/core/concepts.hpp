#pragma once

#include <concepts>
#include <cstddef>

namespace usub::unet::core {

    template<class Buffer>
    concept BufferLike = requires(Buffer &buffer) {
        { buffer.data() } -> std::convertible_to<const void *>;
        { buffer.size() } -> std::convertible_to<std::size_t>;
        { buffer.clear() } -> std::same_as<void>;
    };

    // TODO:
    template<class Stream>
    concept StreamLike = true;

}// namespace usub::unet::core