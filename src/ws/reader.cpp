#include "unet/ws/reader.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

namespace usub::unet::ws {

    namespace {
        // XOR a chunk of payload bytes against the current frame's mask key,
        // accounting for the running index within the frame's payload.
        void unmaskInPlace(std::string &chunk, const std::array<std::uint8_t, 4> &mask, std::size_t index_in_frame) {
            for (std::size_t i = 0; i < chunk.size(); ++i) {
                chunk[i] = static_cast<char>(static_cast<std::uint8_t>(chunk[i]) ^ mask[(index_in_frame + i) % 4]);
            }
        }
    }// namespace

    usub::uvent::task::Awaitable<std::expected<FrameHeader, CLOSE_CODE>> ClientReader::getFrameHeader() {
        auto frame_header = co_await this->channel_->popHeader();
        if (!frame_header) co_return std::unexpected(CLOSE_CODE::ABNORMAL_CLOSE);
        this->current_header_ = *frame_header;
        this->bytes_left_in_frame_ = frame_header->length();
        this->mask_index_ = 0;
        this->partial_.clear();
        this->started_ = true;
        co_return *frame_header;
    }

    usub::uvent::task::Awaitable<std::expected<std::string, CLOSE_CODE>>
    ClientReader::getMaskedPayloadBytes(std::size_t n) {
        if (this->bytes_left_in_frame_ == 0) co_return std::string{};

        const std::size_t want = std::min<std::size_t>(n, this->bytes_left_in_frame_);
        std::string out;
        out.reserve(want);

        if (!this->partial_.empty()) {
            const std::size_t take = std::min(want, this->partial_.size());
            out.append(this->partial_, 0, take);
            this->partial_.erase(0, take);
            this->bytes_left_in_frame_ -= take;
        }

        while (out.size() < want) {
            auto c = co_await this->channel_->popPayload();
            if (!c) co_return std::unexpected(CLOSE_CODE::ABNORMAL_CLOSE);

            const std::size_t remaining = want - out.size();
            if (c->size() <= remaining) {
                this->bytes_left_in_frame_ -= c->size();
                out.append(*c);
            } else {
                out.append(*c, 0, remaining);
                this->partial_ = c->substr(remaining);
                this->bytes_left_in_frame_ -= remaining;
            }
        }

        co_return out;
    }

    usub::uvent::task::Awaitable<std::expected<std::string, CLOSE_CODE>> ClientReader::getPayloadBytes(std::size_t n) {
        const std::size_t mask_offset = this->mask_index_;
        auto masked = co_await this->getMaskedPayloadBytes(n);
        if (!masked) co_return std::unexpected(masked.error());
        if (this->current_header_.masking_key) {
            unmaskInPlace(*masked, *this->current_header_.masking_key, mask_offset);
        }
        this->mask_index_ += masked->size();
        co_return *masked;
    }

    usub::uvent::task::Awaitable<std::expected<std::size_t, CLOSE_CODE>>
    ClientReader::getMaskedPayloadBytes(std::span<std::byte> dst) {
        auto payload = co_await this->getMaskedPayloadBytes(dst.size());
        if (!payload) co_return std::unexpected(payload.error());
        const std::size_t copy_size = std::min(dst.size(), payload->size());
        std::memcpy(dst.data(), payload->data(), copy_size);
        co_return copy_size;
    }

    usub::uvent::task::Awaitable<std::expected<std::size_t, CLOSE_CODE>>
    ClientReader::getPayloadBytes(std::span<std::byte> dst) {
        auto payload = co_await this->getPayloadBytes(dst.size());
        if (!payload) co_return std::unexpected(payload.error());
        const std::size_t copy_size = std::min(dst.size(), payload->size());
        std::memcpy(dst.data(), payload->data(), copy_size);
        co_return copy_size;
    }

    usub::uvent::task::Awaitable<std::expected<std::string, CLOSE_CODE>> ClientReader::getMaskedPayload() {
        co_return co_await this->getMaskedPayloadBytes(static_cast<std::size_t>(this->bytes_left_in_frame_));
    }

    usub::uvent::task::Awaitable<std::expected<std::string, CLOSE_CODE>> ClientReader::getPayload() {
        co_return co_await this->getPayloadBytes(static_cast<std::size_t>(this->bytes_left_in_frame_));
    }

    usub::uvent::task::Awaitable<std::expected<Frame, CLOSE_CODE>> ClientReader::frame() {
        if (!this->started_) {
            auto frame_header = co_await this->getFrameHeader();
            if (!frame_header) co_return std::unexpected(frame_header.error());
        }
        Frame frame;
        static_cast<FrameHeader &>(frame) = this->current_header_;
        auto payload = co_await this->getPayload();
        if (!payload) co_return std::unexpected(payload.error());
        frame.payload = std::move(*payload);
        co_return frame;
    }

    usub::uvent::task::Awaitable<std::expected<std::vector<Frame>, CLOSE_CODE>> ClientReader::accumulate() {
        std::vector<Frame> out;
        for (;;) {
            auto frame = co_await this->frame();
            if (!frame) co_return std::unexpected(frame.error());
            out.push_back(std::move(*frame));
            if (out.back().fin) break;
        }
        co_return out;
    }

    usub::uvent::task::Awaitable<std::expected<std::string, CLOSE_CODE>> ClientReader::accumulateMaskedPayload() {
        std::string out;
        if (!this->started_) {
            auto frame_header = co_await this->getFrameHeader();
            if (!frame_header) co_return std::unexpected(frame_header.error());
        }
        for (;;) {
            auto chunk = co_await this->getMaskedPayload();
            if (!chunk) co_return std::unexpected(chunk.error());
            out.append(*chunk);
            if (this->current_header_.fin) break;
            auto frame_header = co_await this->getFrameHeader();
            if (!frame_header) co_return std::unexpected(frame_header.error());
        }
        co_return out;
    }

    usub::uvent::task::Awaitable<std::expected<std::string, CLOSE_CODE>> ClientReader::accumulatePayload() {
        std::string out;
        if (!this->started_) {
            auto frame_header = co_await this->getFrameHeader();
            if (!frame_header) co_return std::unexpected(frame_header.error());
        }
        for (;;) {
            auto chunk = co_await this->getPayload();
            if (!chunk) co_return std::unexpected(chunk.error());
            out.append(*chunk);
            if (this->current_header_.fin) break;
            auto frame_header = co_await this->getFrameHeader();
            if (!frame_header) co_return std::unexpected(frame_header.error());
        }
        co_return out;
    }

    usub::uvent::task::Awaitable<std::expected<std::string, CLOSE_CODE>>
    ClientReader::accumulateMaskedPayloadBytes(std::size_t n) {
        std::string out;
        out.reserve(n);
        if (!this->started_) {
            auto frame_header = co_await this->getFrameHeader();
            if (!frame_header) co_return std::unexpected(frame_header.error());
        }
        while (out.size() < n) {
            const std::size_t want = n - out.size();
            auto chunk = co_await this->getMaskedPayloadBytes(want);
            if (!chunk) co_return std::unexpected(chunk.error());
            if (chunk->empty()) {
                // current frame exhausted; advance if more remain
                if (this->current_header_.fin) break;
                auto frame_header = co_await this->getFrameHeader();
                if (!frame_header) co_return std::unexpected(frame_header.error());
                continue;
            }
            out.append(*chunk);
        }
        co_return out;
    }

    usub::uvent::task::Awaitable<std::expected<std::size_t, CLOSE_CODE>>
    ClientReader::accumulateMaskedPayloadBytes(std::span<std::byte> dst) {
        auto payload = co_await this->accumulateMaskedPayloadBytes(dst.size());
        if (!payload) co_return std::unexpected(payload.error());
        const std::size_t copy_size = std::min(dst.size(), payload->size());
        std::memcpy(dst.data(), payload->data(), copy_size);
        co_return copy_size;
    }

}// namespace usub::unet::ws
