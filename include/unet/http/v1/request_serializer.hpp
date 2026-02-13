#pragma once

#include <cstdint>
#include <expected>
#include <string>

#include "unet/http/error.hpp"
#include "unet/http/request.hpp"

namespace usub::unet::http::v1 {

    class RequestSerializer {
    public:
        enum class STATE : std::uint8_t {
            REQUEST_LINE,
            HEADERS,
            BODY,
            COMPLETE,
            FAILED,
        };

        struct SerializerContext {
            STATE state{STATE::REQUEST_LINE};
        };

        RequestSerializer() = default;
        ~RequestSerializer() = default;

        SerializerContext &getContext();

        static std::expected<std::string, ParseError> serialize(const Request &request);

    private:
        SerializerContext context_{};
    };
}// namespace usub::unet::http::v1
