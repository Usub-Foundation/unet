#pragma once

#include <string>
#include <string_view>

#include "unet/http/core/request.hpp"
#include "unet/http/error.hpp"

namespace usub::unet::http::v1 {

    class RequestSerializer {
    public:
        enum class STATE { METHOD, URI, VERSION, HEADERS, BODY };

        struct SerializerContext {
            STATE state{};
        };

    public:
        RequestSerializer() = default;
        ~RequestSerializer() = default;

        SerializerContext &getContext();

        static std::string serialize(const Request &request);

    private:
        SerializerContext context_;
    };

}// namespace usub::unet::http::v1
