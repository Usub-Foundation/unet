#pragma once

#include <string_view>

#include <uvent/Uvent.h>

#include "unet/http/callback.hpp"
#include "unet/http/request.hpp"
#include "unet/http/response.hpp"
#include "unet/http/v1/request_serializer.hpp"
#include "unet/http/v1/response_parser.hpp"

namespace usub::unet::http::v1 {
    class EgressSession {
    public:
        EgressSession() = default;
        ~EgressSession() = default;

        usub::uvent::task::Awaitable<void> process(std::string_view data, AsyncCallback &callbacks);

    private:
        Request request_{};
        Response response_{};
        v1::ResponseParser response_reader_{};
        v1::RequestSerializer request_writer_{};
    };
}// namespace usub::unet::http::v1