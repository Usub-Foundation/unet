#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <uvent/sync/AsyncChannel.h>
#include <uvent/tasks/Awaitable.h>

#include "unet/header/generic.hpp"
#include "unet/http/core/body_reader.hpp"
#include "unet/http/core/message.hpp"


namespace usub::unet::http {

    struct ResponseMetadata {
        VERSION version{};
        std::uint16_t status_code{};
        std::optional<std::string> status_message{};
    };

    struct Response;

    struct ResponseWriter {
        enum class Mode : std::uint8_t {
            Empty,
            Chunked,
            Sent,
            Aborted,
        };

        struct Ops {
            std::function<usub::uvent::task::Awaitable<bool>(std::string)> send_body{};
            std::function<usub::uvent::task::Awaitable<bool>(int, std::uint64_t, std::uint64_t)> send_file{};
            std::function<usub::uvent::task::Awaitable<bool>()> chunk_start{};
            std::function<usub::uvent::task::Awaitable<bool>(std::string)> chunk_write{};
            std::function<usub::uvent::task::Awaitable<bool>(int, std::uint64_t, std::uint64_t)> chunk_file{};
            std::function<usub::uvent::task::Awaitable<bool>()> chunk_end{};
        };

        ResponseMetadata metadata{};
        usub::unet::header::Headers headers{};
        usub::unet::header::Headers trailers{};

        ResponseWriter() = default;
        ResponseWriter(const ResponseWriter &) = delete;
        ResponseWriter &operator=(const ResponseWriter &) = delete;
        ResponseWriter(ResponseWriter &&) = default;
        ResponseWriter &operator=(ResponseWriter &&) = default;

        void bindOps(Ops ops) noexcept { ops_ = std::move(ops); }
        Mode mode() const noexcept { return mode_; }

        usub::uvent::task::Awaitable<bool> send(std::string s);

        usub::uvent::task::Awaitable<bool> file(int fd, std::uint64_t length, std::uint64_t offset = 0);

        usub::uvent::task::Awaitable<bool> start();

        usub::uvent::task::Awaitable<bool> chunk(std::string s);

        usub::uvent::task::Awaitable<bool> chunk(int fd, std::uint64_t length, std::uint64_t offset = 0);

        usub::uvent::task::Awaitable<bool> end();

        usub::uvent::task::Awaitable<void> abort() noexcept;

        usub::uvent::task::Awaitable<bool> send(Response resp);

    private:
        Mode mode_{Mode::Empty};
        Ops ops_{};
    };


    struct ResponseReader {
        ResponseMetadata metadata{};
        usub::unet::header::Headers headers{};
        usub::unet::header::Headers trailers{};

        ResponseReader();

        ResponseReader(const ResponseReader &) = delete;
        ResponseReader &operator=(const ResponseReader &) = delete;
        ResponseReader(ResponseReader &&) = default;
        ResponseReader &operator=(ResponseReader &&) = default;

        usub::uvent::task::Awaitable<std::expected<std::optional<std::string>, BODY_ERROR>> chunk();

        usub::uvent::task::Awaitable<std::expected<std::string, BODY_ERROR>> readBody();

        usub::uvent::task::Awaitable<std::expected<std::string, BODY_ERROR>> collect(std::size_t limit);

        bool eof() const noexcept { return eof_ && residual_.empty(); }

        BodyReaderChannel &getBodyChannel() noexcept { return *channel_; }

    private:
        std::shared_ptr<BodyReaderChannel> channel_;
        std::string residual_{};
        bool eof_{false};
    };


    struct Response {
        ResponseMetadata metadata{};
        usub::unet::header::Headers headers{};
        std::string body{};
        usub::unet::header::Headers trailers{};
    };


}// namespace usub::unet::http
