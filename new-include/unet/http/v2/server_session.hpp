#pragma once

#include <chrono>
#include <cstdint>
#include <cstring>
#include <expected>
#include <memory>
#include <queue>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "unet/http/request.hpp"
#include "unet/http/response.hpp"
#include "unet/http/router/route.hpp"
#include "unet/http/session.hpp"
#include "unet/http/v2/connection.hpp"
#include "unet/http/v2/error.hpp"
#include "unet/http/v2/frames.hpp"
#include "unet/http/v2/response_serializer.hpp"
#include "unet/http/v2/stream.hpp"


namespace usub::unet::http {

    template<typename RouterType>
    class ServerSession<VERSION::HTTP_2_0, RouterType> {
    public:
        explicit ServerSession(std::shared_ptr<RouterType> router) : connection_(std::move(router)) {
            // this->connection_parser_.set_max_frame_size(this->server_settings_effective_.max_frame_size);
        }
        ServerSession() = delete;
        ~ServerSession() = default;

        usub::uvent::task::Awaitable<void> on_read(std::string_view data,
                                                   usub::unet::core::stream::Transport &transport) {
            co_await this->connection_.on_read(data, transport);
        }

        usub::uvent::task::Awaitable<void> on_close() { co_return; }

        usub::uvent::task::Awaitable<void> on_error(int) { co_return; }

    private:
        v2::Connection<RouterType> connection_;
    };
}// namespace usub::unet::http
