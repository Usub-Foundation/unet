#pragma once

#include <array>
#include <string>


namespace usub::unet::utils {

    struct UnetError {
        enum CODE {
            DEFAULT = 0,
        };
        CODE error_code;
        std::string message;
        std::array<char, 256> tail;
    };

}// namespace usub::unet::utils
