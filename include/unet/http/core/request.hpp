#pragma once

#include <any>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>

#include <uvent/sync/AsyncChannel.h>
#include <uvent/tasks/Awaitable.h>

#include "unet/header/generic.hpp"
#include "unet/http/core/body_reader.hpp"
#include "unet/http/core/message.hpp"
#include "unet/http/header.hpp"
#include "unet/http/peer_info.hpp"
#include "unet/mime/application/x_www_form_urlencoded.hpp"
#include "unet/uri/uri.hpp"


namespace usub::unet::http {

    extern std::uint8_t max_method_token_size;
    extern std::uint16_t max_uri_size;

    struct RequestMetadata {
        std::string method_token{};
        uri::URI uri{};
        VERSION version{};
        std::string authority;
    };

    struct RequestReader {
        using QueryParams = usub::unet::mime::application::x_www_form_urlencoded::FieldMap;

        RequestMetadata metadata{};
        usub::unet::header::Headers headers{};
        usub::unet::header::Headers trailers{};
        // TODO: experimental, subject to change.
        PeerInfo peer{};
        std::any user_data{};

        RequestReader();

        RequestReader(const RequestReader &) = delete;
        RequestReader &operator=(const RequestReader &) = delete;
        RequestReader(RequestReader &&) = default;
        RequestReader &operator=(RequestReader &&) = default;

        template<typename ReturnType = std::string>
        ReturnType getQueryAs() {
            return ReturnType(this->metadata.uri.query);
        }

        usub::uvent::task::Awaitable<std::expected<std::optional<std::string>, BODY_ERROR>> chunk();

        usub::uvent::task::Awaitable<std::expected<std::string, BODY_ERROR>> readBody();

        usub::uvent::task::Awaitable<std::expected<std::string, BODY_ERROR>> readBodyBytes(std::size_t n);

        usub::uvent::task::Awaitable<std::expected<std::size_t, BODY_ERROR>> readBodyBytes(std::span<std::byte> dst);

        usub::uvent::task::Awaitable<std::expected<std::string, BODY_ERROR>> collect(std::size_t limit);

        bool eof() const noexcept { return eof_ && residual_.empty(); }

        BodyReaderChannel &getBodyChannel() noexcept { return *channel_; }

        // Shares the underlying channel so an upgraded protocol's transport can read the
        // same byte stream the handler was reading (or would have read).
        std::shared_ptr<BodyReaderChannel> getBodyChannelPtr() const noexcept { return channel_; }

    private:
        std::shared_ptr<BodyReaderChannel> channel_;
        std::string residual_{};
        bool eof_{false};
    };


    struct Request;

    struct RequestWriter {
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

        RequestMetadata metadata{};
        usub::unet::header::Headers headers{};
        usub::unet::header::Headers trailers{};

        RequestWriter() = default;
        RequestWriter(const RequestWriter &) = delete;
        RequestWriter &operator=(const RequestWriter &) = delete;
        RequestWriter(RequestWriter &&) = default;
        RequestWriter &operator=(RequestWriter &&) = default;

        void bindOps(Ops ops) noexcept { ops_ = std::move(ops); }
        Mode mode() const noexcept { return mode_; }

        usub::uvent::task::Awaitable<bool> send(std::string s);

        usub::uvent::task::Awaitable<bool> file(int fd, std::uint64_t length, std::uint64_t offset = 0);

        usub::uvent::task::Awaitable<bool> start();

        usub::uvent::task::Awaitable<bool> chunk(std::string s);

        usub::uvent::task::Awaitable<bool> chunk(int fd, std::uint64_t length, std::uint64_t offset = 0);

        usub::uvent::task::Awaitable<bool> end();

        usub::uvent::task::Awaitable<void> abort() noexcept;

        usub::uvent::task::Awaitable<bool> send(Request req);

    private:
        Mode mode_{Mode::Empty};
        Ops ops_{};
    };

    // Client only, pending rewrite.
    struct Request {
        RequestMetadata metadata{};
        usub::unet::header::Headers headers{};
        std::string body{};
        usub::unet::header::Headers trailers{};
    };

}// namespace usub::unet::http
