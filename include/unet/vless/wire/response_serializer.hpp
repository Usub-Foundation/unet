#pragma once

#include <string>

#include "unet/vless/core/response.hpp"

namespace usub::unet::vless {

    class ResponseSerializer {
    public:
        static std::string serialize(const Response &response);
    };

}// namespace usub::unet::vless
