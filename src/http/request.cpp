#include "unet/http/request.hpp"

namespace usub::unet::http {
    std::uint8_t max_method_token_size =
            std::numeric_limits<std::uint8_t>::max();// Arguably should be much smaller, but let's be VERY generous
    std::uint16_t max_uri_size = std::numeric_limits<std::uint16_t>::max();// Very generous limit for URI
}// namespace usub::unet::http