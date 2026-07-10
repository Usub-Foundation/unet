#pragma once

#include <cstddef>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <uvent/Uvent.h>
#include <uvent/sync/AsyncChannel.h>

#include "unet/core/io_provider.hpp"
#include "unet/ws/core/frame.hpp"

namespace usub::unet::ws {

    class ClientReaderChannel {
    public:
        explicit ClientReaderChannel(std::size_t header_cap_pow2 = 8, std::size_t payload_cap_pow2 = 64)
            : headers_(header_cap_pow2), payload_(payload_cap_pow2) {}

        ClientReaderChannel(const ClientReaderChannel &) = delete;
        ClientReaderChannel &operator=(const ClientReaderChannel &) = delete;

        usub::uvent::task::Awaitable<bool> pushHeader(FrameHeader h) {
            co_return co_await this->headers_.send(std::move(h));
        }

        usub::uvent::task::Awaitable<bool> pushPayload(std::string chunk) {
            co_return co_await this->payload_.send(std::move(chunk));
        }

        usub::uvent::task::Awaitable<std::optional<FrameHeader>> popHeader() {
            auto v = co_await this->headers_.recv();
            if (!v) co_return std::nullopt;
            auto &[h] = *v;
            co_return std::optional<FrameHeader>{std::move(h)};
        }

        usub::uvent::task::Awaitable<std::optional<std::string>> popPayload() {
            auto v = co_await this->payload_.recv();
            if (!v) co_return std::nullopt;
            auto &[c] = *v;
            co_return std::optional<std::string>{std::move(c)};
        }

        void close() noexcept {
            this->headers_.close();
            this->payload_.close();
        }

        [[nodiscard]] bool is_closed() const noexcept {
            return this->headers_.is_closed() && this->payload_.is_closed();
        }

    private:
        usub::uvent::sync::AsyncChannel<FrameHeader> headers_;
        usub::uvent::sync::AsyncChannel<std::string> payload_;
    };

    class ClientReader {
    public:
        explicit ClientReader(std::shared_ptr<ClientReaderChannel> ch) noexcept : channel_(std::move(ch)) {}


        usub::uvent::task::Awaitable<std::expected<Frame, CLOSE_CODE>> frame();

        usub::uvent::task::Awaitable<std::expected<std::string, CLOSE_CODE>> getMaskedPayload();

        usub::uvent::task::Awaitable<std::expected<std::string, CLOSE_CODE>> getPayload();

        usub::uvent::task::Awaitable<std::expected<FrameHeader, CLOSE_CODE>> getFrameHeader();

        usub::uvent::task::Awaitable<std::expected<std::string, CLOSE_CODE>> getMaskedPayloadBytes(std::size_t n);

        usub::uvent::task::Awaitable<std::expected<std::size_t, CLOSE_CODE>>
        getMaskedPayloadBytes(std::span<std::byte> dst);

        usub::uvent::task::Awaitable<std::expected<std::string, CLOSE_CODE>> getPayloadBytes(std::size_t n);

        usub::uvent::task::Awaitable<std::expected<std::size_t, CLOSE_CODE>> getPayloadBytes(std::span<std::byte> dst);

        usub::uvent::task::Awaitable<std::expected<std::vector<Frame>, CLOSE_CODE>> accumulate();

        usub::uvent::task::Awaitable<std::expected<std::string, CLOSE_CODE>> accumulateMaskedPayload();

        usub::uvent::task::Awaitable<std::expected<std::string, CLOSE_CODE>> accumulatePayload();

        usub::uvent::task::Awaitable<std::expected<std::string, CLOSE_CODE>>
        accumulateMaskedPayloadBytes(std::size_t n);

        usub::uvent::task::Awaitable<std::expected<std::size_t, CLOSE_CODE>>
        accumulateMaskedPayloadBytes(std::span<std::byte> dst);


        [[nodiscard]] std::uint64_t bytesLeftInFrame() const noexcept { return this->bytes_left_in_frame_; }
        [[nodiscard]] const FrameHeader &currentHeader() const noexcept { return this->current_header_; }

    private:
        std::shared_ptr<ClientReaderChannel> channel_;
        FrameHeader current_header_{};
        std::string partial_{};
        std::uint64_t bytes_left_in_frame_{0};


        std::size_t mask_index_{0};
        bool started_{false};
    };

}// namespace usub::unet::ws
