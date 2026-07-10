#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <utility>

#include <uvent/sync/AsyncChannel.h>
#include <uvent/tasks/Awaitable.h>


namespace usub::unet::http {

    enum class BODY_ERROR : std::uint8_t {
        NONE,
        PROTOCOL_ERROR,
        PARSER_ERROR,
        FRAME_SIZE_ERROR,
        INTERNAL_ERROR,
    };


    class BodyReaderChannel {
    public:
        explicit BodyReaderChannel(std::size_t capacity_pow2 = 16) : channel_(capacity_pow2) {}

        BodyReaderChannel(const BodyReaderChannel &)            = delete;
        BodyReaderChannel &operator=(const BodyReaderChannel &) = delete;

        usub::uvent::task::Awaitable<bool> push(std::string chunk) {
            co_return co_await channel_.send(std::move(chunk));
        }
        usub::uvent::task::Awaitable<std::expected<std::optional<std::string>, BODY_ERROR>> pop() {
            auto v = co_await channel_.recv();
            if (errored_.load(std::memory_order_acquire))
                co_return std::unexpected(error_.load(std::memory_order_acquire));
            if (!v) co_return std::optional<std::string>{};
            auto &[chunk] = *v;
            co_return std::optional<std::string>{std::move(chunk)};
        }

        void close() noexcept { channel_.close(); }
        void signalError(BODY_ERROR code) noexcept {
            error_.store(code, std::memory_order_release);
            errored_.store(true, std::memory_order_release);
            channel_.close();
        }
        bool isClosed() const noexcept { return channel_.is_closed(); }

    private:
        usub::uvent::sync::AsyncChannel<std::string> channel_;
        std::atomic<bool>                            errored_{false};
        std::atomic<BODY_ERROR>                      error_{BODY_ERROR::NONE};
    };

}// namespace usub::unet::http
