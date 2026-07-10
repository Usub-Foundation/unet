#include "unet/http/core/response.hpp"

namespace usub::unet::http {

    ResponseReader::ResponseReader() : channel_(std::make_shared<BodyReaderChannel>()) {}

    usub::uvent::task::Awaitable<std::expected<std::optional<std::string>, BODY_ERROR>> ResponseReader::chunk() {
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

    usub::uvent::task::Awaitable<std::expected<std::string, BODY_ERROR>> ResponseReader::readBody() {
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

    usub::uvent::task::Awaitable<std::expected<std::string, BODY_ERROR>> ResponseReader::collect(std::size_t limit) {
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

    usub::uvent::task::Awaitable<bool> ResponseWriter::send(std::string s) {
        if (mode_ != Mode::Empty) co_return false;
        mode_ = Mode::Sent;
        if (!ops_.send_body) co_return false;
        co_return co_await ops_.send_body(std::move(s));
    }

    usub::uvent::task::Awaitable<bool> ResponseWriter::file(int fd, std::uint64_t length, std::uint64_t offset) {
        if (mode_ != Mode::Empty) co_return false;
        mode_ = Mode::Sent;
        if (!ops_.send_file) co_return false;
        co_return co_await ops_.send_file(fd, length, offset);
    }

    usub::uvent::task::Awaitable<bool> ResponseWriter::start() {
        if (mode_ != Mode::Empty) co_return false;
        mode_ = Mode::Chunked;
        if (!ops_.chunk_start) co_return false;
        co_return co_await ops_.chunk_start();
    }


    usub::uvent::task::Awaitable<bool> ResponseWriter::chunk(std::string s) {
        if (mode_ != Mode::Chunked) co_return false;
        if (!ops_.chunk_write) co_return false;
        co_return co_await ops_.chunk_write(std::move(s));
    }

    usub::uvent::task::Awaitable<bool> ResponseWriter::chunk(int fd, std::uint64_t length, std::uint64_t offset) {
        if (mode_ != Mode::Chunked) co_return false;
        if (!ops_.chunk_file) co_return false;
        co_return co_await ops_.chunk_file(fd, length, offset);
    }

    usub::uvent::task::Awaitable<bool> ResponseWriter::end() {
        if (mode_ != Mode::Chunked) co_return false;
        mode_ = Mode::Sent;
        if (!ops_.chunk_end) co_return false;
        co_return co_await ops_.chunk_end();
    }

    usub::uvent::task::Awaitable<void> ResponseWriter::abort() noexcept {
        mode_ = Mode::Aborted;
        co_return;
    }

    usub::uvent::task::Awaitable<bool> ResponseWriter::send(Response resp) {
        this->metadata = std::move(resp.metadata);
        for (auto &h: resp.headers.all()) this->headers.addHeader(h.key, h.value);
        for (auto &h: resp.trailers.all()) this->trailers.addHeader(h.key, h.value);
        co_return co_await this->send(std::move(resp.body));
    }


}// namespace usub::unet::http