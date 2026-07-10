#pragma once

// TODO: h2 client — preface + SETTINGS + HEADERS/DATA + WINDOW_UPDATE.

#include <expected>
#include <memory>
#include <utility>

#include <uvent/Uvent.h>

#include "unet/core/transport/transport.hpp"
#include "unet/http/client_error.hpp"
#include "unet/http/core/request.hpp"
#include "unet/http/core/response.hpp"


namespace usub::unet::http::v2 {

    class ClientSession {
    public:
        usub::uvent::task::Awaitable<std::expected<ResponseReader, ClientError>>
        run(std::unique_ptr<usub::unet::core::transport::Transport> /*transport*/,
            Request /*req*/) {
            co_return std::unexpected(ClientError::ProtocolError);
        }
    };

}// namespace usub::unet::http::v2
