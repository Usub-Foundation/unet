#pragma once

#include <any>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>


#include "unet/http/core/message.hpp"
#include "unet/http/header.hpp"
#include "unet/mime/application/x_www_form_urlencoded.hpp"
#include "unet/uri/uri.hpp"


namespace usub::unet::http {

    extern std::uint8_t max_method_token_size;
    extern std::uint16_t max_uri_size;

    struct RequestMetadata {
        std::string method_token{};
        uri::URI uri{};
        VERSION version{};
        std::string authority;// :authority: http2, Host header duplication in http/1.1
    };

    struct Request {
        using QueryParams = usub::unet::mime::application::x_www_form_urlencoded::FieldMap;

        RequestMetadata metadata{};
        usub::unet::header::Headers headers{};
        std::string body{};

        std::any user_data{};// A place to store content between middlewares
        QueryParams query_params{};
        std::string query_params_source{};

        template<typename ReturnType = std::string>
        ReturnType getQueryAs() {
            return ReturnType(this->metadata.uri.query);
        }

        //COMPAT
        QueryParams &getQueryParams() {
            if (this->query_params_source == this->metadata.uri.query) {
                return this->query_params;
            }

            this->query_params_source = this->metadata.uri.query;

            auto parsed = usub::unet::mime::application::x_www_form_urlencoded::parse_to_map(this->metadata.uri.query);
            this->query_params = parsed ? std::move(*parsed) : QueryParams{};

            return this->query_params;
        }

        //COMPAT
        std::string &getBody() {
            return this->body;
        }

    };

}// namespace usub::unet::http
