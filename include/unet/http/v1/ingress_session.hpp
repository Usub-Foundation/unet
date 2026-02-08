#pragma once

#include <string_view>

#include <uvent/Uvent.h>

#include "unet/http/callback.hpp"
#include "unet/http/request.hpp"
#include "unet/http/response.hpp"
#include "unet/http/router/route.hpp"
#include "unet/http/session.hpp"
#include "unet/http/v1/request_parser.hpp"
#include "unet/http/v1/response_serializer.hpp"

namespace usub::unet::http::v1 {
    class IngressSession {
    public:
        IngressSession() = default;
        ~IngressSession() = default;

        usub::uvent::task::Awaitable<void> process(std::string_view data, const AsyncCallback &callbacks);

    private:
        Request request_{};
        Response response_{};
        v1::RequestParser request_reader_{};
        v1::ResponseSerializer response_writer_{};
    };

}// namespace usub::unet::http::v1
