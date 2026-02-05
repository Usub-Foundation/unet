#pragma once

#include "unet/http/request.hpp"
#include "unet/http/response.hpp"
#include "unet/http/router/route.hpp"
#include "unet/http/session.hpp"
#include "unet/http/v1/request_parser.hpp"
#include "unet/http/v1/response_serializer.hpp"

namespace usub::unet::http::v1 {
    class ServerStream {
    public:
        ServerStream() = default;
        ~ServerStream() = default;

        // enum class STATE { READ_REQUEST, WRITE_RESPONSE, CLOSED, ERROR };

        // struct ConnectionContext {
        //     STATE state{STATE::READ_REQUEST};
        // };

        // ConnectionContext &getContext() { return this->context_; }

    private:
        Request request_{};
        Response response_{};
        v1::RequestParser request_reader_{};
        v1::ResponseSerializer response_writer_{};
    };
}// namespace usub::unet::http::v1
