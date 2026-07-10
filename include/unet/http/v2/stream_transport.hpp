#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <uvent/Uvent.h>

#include "unet/core/io_provider.hpp"
#include "unet/core/transport/transport.hpp"
#include "unet/http/core/body_reader.hpp"

namespace usub::unet::http::v2 {

    class StreamTransport final : public usub::unet::core::transport::Transport {
    public:
        struct Ops {
            std::function<usub::uvent::task::Awaitable<ssize_t>(std::string)> send_payload;
            std::function<usub::uvent::task::Awaitable<void>()> close_stream;
        };

        StreamTransport(Ops ops, std::shared_ptr<BodyReaderChannel> body) noexcept
            : ops_(std::move(ops)), body_(std::move(body)) {}

        StreamTransport(const StreamTransport &) = delete;
        StreamTransport &operator=(const StreamTransport &) = delete;

        usub::uvent::task::Awaitable<ssize_t> read(usub::unet::Buffer &dst) override {
            auto v = co_await this->body_->pop();
            if (!v) co_return -1;             // BODY_ERROR
            if (!v->has_value()) co_return -1;// channel closed / EOF
            const std::string &chunk = **v;
            if (chunk.empty()) co_return 0;
            std::uint8_t *p = dst.append_raw(chunk.size());
            std::memcpy(p, chunk.data(), chunk.size());
            co_return static_cast<ssize_t>(chunk.size());
        }

        usub::uvent::task::Awaitable<ssize_t> send(std::string_view bytes) override {
            co_return co_await this->ops_.send_payload(std::string(bytes));
        }

        usub::uvent::task::Awaitable<bool> sendFile(int fd, std::uint64_t length, std::uint64_t offset) override {
            constexpr std::size_t kReadChunk = 64u * 1024u;
            std::string buf;
            buf.resize(kReadChunk);
            std::uint64_t remaining = length;
            // TODO: honour offset via lseek/_lseeki64.
            (void) offset;
            while (remaining > 0) {
                const std::size_t want = std::min<std::size_t>(kReadChunk, remaining);
                ssize_t got = ::read(fd, buf.data(), want);
                if (got <= 0) co_return false;
                std::string chunk(buf.data(), static_cast<std::size_t>(got));
                const auto sent = co_await this->ops_.send_payload(std::move(chunk));
                if (sent < 0) co_return false;
                remaining -= static_cast<std::uint64_t>(got);
            }
            co_return true;
        }

        usub::uvent::task::Awaitable<void> shutdown() override {
            co_await this->ops_.close_stream();
            this->body_->close();
            co_return;
        }

    private:
        Ops ops_;
        std::shared_ptr<BodyReaderChannel> body_;
    };

}// namespace usub::unet::http::v2
