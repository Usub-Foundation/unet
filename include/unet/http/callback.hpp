#pragma once

#include <string_view>

#include "unet/http/request.hpp"
#include "unet/http/response.hpp"
#include "unet/http/router/route.hpp"

namespace usub::unet::http {
    struct AsyncCallback {
        std::function<bool(Request &, Response &)> on_headers_done;
        std::function<bool(Request &, Response &)> on_body_chunk_done;
        // std::function<bool(Request&, Response&)> on_request_complete;
        // std::function<bool(Request&, Response&)> on_trailers_done;

        std::function<bool(std::string, Request &, Response &)> on_error;

        std::function<usub::uvent::task::Awaitable<void>(Request &, Response &)> invoke_handler;

        std::function<std::expected<router::Route *, STATUS_CODE>(Request &)> set_route;

        std::function<usub::uvent::task::Awaitable<void>(std::string_view data)> send;
    };

};// namespace usub::unet::http