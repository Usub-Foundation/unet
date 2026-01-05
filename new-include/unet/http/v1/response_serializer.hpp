#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <expected>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "unet/http/error.hpp"
#include "unet/http/response.hpp"

namespace usub::unet::http::v1 {

    class ResponseSerializer {
    public:
        enum class STATE {
            STATUS_CODE,
            STATUS_MESSAGE,
            HEADERS,// TODO: ????
            BODY

        };

        struct SerializerContext {
            STATE state{};
        };

    public:
        ResponseSerializer() = default;
        ~ResponseSerializer() = default;


        SerializerContext &getContext();

        static std::string serialize(const Response &response);

    private:
        SerializerContext context_;
    };
}// namespace usub::unet::http::v1
