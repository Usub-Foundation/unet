#pragma once

#include <any>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>


#include "unet/http/header.hpp"
#include "unet/http/message.hpp"
#include "unet/uri/uri.hpp"

namespace usub::unet::http {

    static std::uint8_t max_method_token_size;
    static std::uint16_t max_uri_size;

    struct RequestMetadata {
        std::string method_token{};
        uri::URI uri{};
        VERSION version{};
        std::string authority;// :authority: http2, Host header duplication in http/1.1
    };

    struct Request {
        RequestMetadata metadata{};
        usub::unet::header::Headers headers{};
        std::string body{};

        std::unordered_map<std::string, std::string> uri_params{};
        std::any user_data{};// A place to store content between middlewares

        MessagePolicy policy{};// keep it last for easier initialization

        template<typename ReturnType = std::string>
        ReturnType getQueryAs() {
            return ReturnType(this->metadata.uri.query);
        }
    };

}// namespace usub::unet::http