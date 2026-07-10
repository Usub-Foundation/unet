#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string>

#include <uvent/tasks/Awaitable.h>

#include "unet/http/core/response.hpp"

namespace usub::unet::http::v1 {


    class ResponseSerializer {
    public:
        enum class STATE : std::uint8_t {
            INIT,
            HEADERS_SENT,
            BODY_WRITING,
            DONE,
        };

        struct SerializerContext {
            STATE state{STATE::INIT};
        };

        using SinkFn = std::function<usub::uvent::task::Awaitable<bool>(std::string)>;

        ResponseSerializer() = default;
        ~ResponseSerializer() = default;

        explicit ResponseSerializer(SinkFn sink) noexcept : sink_(std::move(sink)) {}

        SerializerContext &getContext() noexcept { return context_; }
        const SerializerContext &getContext() const noexcept { return context_; }

        static std::string serialize(const ResponseWriter &response);

        usub::uvent::task::Awaitable<bool> sendBody(const ResponseWriter &response, std::string body);

        usub::uvent::task::Awaitable<bool> sendBodyHead(const ResponseWriter &response, std::uint64_t content_length);

        usub::uvent::task::Awaitable<bool> sendHeaders(const ResponseWriter &response);
        usub::uvent::task::Awaitable<bool> writeChunk(const ResponseWriter &response, std::span<const std::byte> data);
        usub::uvent::task::Awaitable<bool> writeChunkHeader(std::size_t len);// "<hex>\r\n"
        usub::uvent::task::Awaitable<bool> writeChunkTrailer();              // "\r\n"
        usub::uvent::task::Awaitable<bool> end(const ResponseWriter &response);

    private:
        SinkFn sink_{};
        SerializerContext context_{};
    };
}// namespace usub::unet::http::v1
