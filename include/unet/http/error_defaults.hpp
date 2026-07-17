#pragma once

#include <string>
#include <string_view>

#include <uvent/tasks/Awaitable.h>

#include "unet/http/core/message.hpp"
#include "unet/http/core/request.hpp"
#include "unet/http/core/response.hpp"

namespace usub::unet::http {

    inline usub::uvent::task::Awaitable<void> defaultErrorResponse(RequestReader &, ResponseWriter &res) {
        if (res.mode() != ResponseWriter::Mode::Empty) co_return;

        const std::uint16_t code = res.metadata.status_code
                                           ? res.metadata.status_code
                                           : static_cast<std::uint16_t>(STATUS_CODE::INTERNAL_SERVER_ERROR);
        res.metadata.status_code = code;

        std::string_view name = (code < status_messages.size()) ? status_messages[code] : std::string_view{};
        std::string body = std::to_string(code);
        if (!name.empty()) {
            body += ' ';
            body.append(name);
        }
        body += '\n';

        if (!res.headers.contains("content-type")) {
            res.headers.addHeader(std::string_view{"content-type"}, std::string_view{"text/plain; charset=utf-8"});
        }
        co_await res.send(std::move(body));
    }

}// namespace usub::unet::http
