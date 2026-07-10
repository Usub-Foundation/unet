#include "unet/http/core/request.hpp"

#include <cstring>
#include <limits>

namespace usub::unet::http {

    std::uint8_t max_method_token_size = std::numeric_limits<std::uint8_t>::max();
    std::uint16_t max_uri_size = std::numeric_limits<std::uint16_t>::max();

    RequestReader::RequestReader() : channel_(std::make_shared<BodyReaderChannel>()) {}

    usub::uvent::task::Awaitable<std::expected<std::optional<std::string>, BODY_ERROR>> RequestReader::chunk() {
        if (eof_) co_return std::optional<std::string>{};
        if (!residual_.empty()) {
            std::string out{std::move(residual_)};
            residual_.clear();
            co_return std::optional<std::string>{std::move(out)};
        }
        auto next = co_await channel_->pop();
        if (!next) co_return std::unexpected(next.error());
        if (!next->has_value()) {
            eof_ = true;
            co_return std::optional<std::string>{};
        }
        co_return std::optional<std::string>{std::move(**next)};
    }

    usub::uvent::task::Awaitable<std::expected<std::string, BODY_ERROR>> RequestReader::readBody() {
        std::string out{std::move(residual_)};
        residual_.clear();
        while (!eof_) {
            auto next = co_await channel_->pop();
            if (!next) co_return std::unexpected(next.error());
            if (!next->has_value()) {
                eof_ = true;
                break;
            }
            out += **next;
        }
        co_return out;
    }

    usub::uvent::task::Awaitable<std::expected<std::string, BODY_ERROR>> RequestReader::readBodyBytes(std::size_t n) {
        std::string out;
        if (!residual_.empty()) {
            const std::size_t take = std::min(n, residual_.size());
            out.append(residual_.data(), take);
            residual_ = (take == residual_.size()) ? std::string{} : residual_.substr(take);
        }
        while (out.size() < n && !eof_) {
            auto next = co_await channel_->pop();
            if (!next) co_return std::unexpected(next.error());
            if (!next->has_value()) {
                eof_ = true;
                break;
            }
            const std::size_t want = n - out.size();
            if (next->value().size() <= want) {
                out += **next;
            } else {
                out.append(next->value().data(), want);
                residual_ = next->value().substr(want);
            }
        }
        co_return out;
    }

    usub::uvent::task::Awaitable<std::expected<std::size_t, BODY_ERROR>>
    RequestReader::readBodyBytes(std::span<std::byte> dst) {
        std::size_t filled = 0;
        while (filled < dst.size()) {
            if (residual_.empty()) {
                if (eof_) co_return filled;
                auto next = co_await channel_->pop();
                if (!next) co_return std::unexpected(next.error());
                if (!next->has_value()) {
                    eof_ = true;
                    co_return filled;
                }
                residual_ = std::move(**next);
            }
            const std::size_t take = std::min(dst.size() - filled, residual_.size());
            std::memcpy(dst.data() + filled, residual_.data(), take);
            filled += take;
            residual_ = (take == residual_.size()) ? std::string{} : residual_.substr(take);
        }
        co_return filled;
    }

    usub::uvent::task::Awaitable<std::expected<std::string, BODY_ERROR>> RequestReader::collect(std::size_t limit) {
        if (residual_.size() > limit) co_return std::unexpected(BODY_ERROR::FRAME_SIZE_ERROR);
        std::string out{std::move(residual_)};
        residual_.clear();
        while (!eof_) {
            auto next = co_await channel_->pop();
            if (!next) co_return std::unexpected(next.error());
            if (!next->has_value()) {
                eof_ = true;
                break;
            }
            if (out.size() + next->value().size() > limit) co_return std::unexpected(BODY_ERROR::FRAME_SIZE_ERROR);
            out += **next;
        }
        co_return out;
    }

    usub::uvent::task::Awaitable<bool> RequestWriter::send(std::string s) {
        if (mode_ != Mode::Empty) co_return false;
        mode_ = Mode::Sent;
        if (!ops_.send_body) co_return false;
        co_return co_await ops_.send_body(std::move(s));
    }

    usub::uvent::task::Awaitable<bool> RequestWriter::file(int fd, std::uint64_t length, std::uint64_t offset) {
        if (mode_ != Mode::Empty) co_return false;
        mode_ = Mode::Sent;
        if (!ops_.send_file) co_return false;
        co_return co_await ops_.send_file(fd, length, offset);
    }

    usub::uvent::task::Awaitable<bool> RequestWriter::start() {
        if (mode_ != Mode::Empty) co_return false;
        mode_ = Mode::Chunked;
        if (!ops_.chunk_start) co_return false;
        co_return co_await ops_.chunk_start();
    }

    usub::uvent::task::Awaitable<bool> RequestWriter::chunk(std::string s) {
        if (mode_ != Mode::Chunked) co_return false;
        if (!ops_.chunk_write) co_return false;
        co_return co_await ops_.chunk_write(std::move(s));
    }

    usub::uvent::task::Awaitable<bool> RequestWriter::chunk(int fd, std::uint64_t length, std::uint64_t offset) {
        if (mode_ != Mode::Chunked) co_return false;
        if (!ops_.chunk_file) co_return false;
        co_return co_await ops_.chunk_file(fd, length, offset);
    }

    usub::uvent::task::Awaitable<bool> RequestWriter::end() {
        if (mode_ != Mode::Chunked) co_return false;
        mode_ = Mode::Sent;
        if (!ops_.chunk_end) co_return false;
        co_return co_await ops_.chunk_end();
    }

    usub::uvent::task::Awaitable<void> RequestWriter::abort() noexcept {
        mode_ = Mode::Aborted;
        co_return;
    }

    usub::uvent::task::Awaitable<bool> RequestWriter::send(Request req) {
        this->metadata = std::move(req.metadata);
        for (auto &h: req.headers.all()) this->headers.addHeader(h.key, h.value);
        for (auto &h: req.trailers.all()) this->trailers.addHeader(h.key, h.value);
        co_return co_await this->send(std::move(req.body));
    }

}// namespace usub::unet::http
