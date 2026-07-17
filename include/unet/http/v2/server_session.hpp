#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <expected>
#include <functional>
#include <memory>
#include <queue>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <uvent/Uvent.h>
#include <uvent/sync/AsyncChannel.h>
#include <uvent/sync/AsyncEvent.h>
#include <uvent/sync/SyncCommon.h>

#include "unet/core/transport/transport.hpp"
#include "unet/http/core/request.hpp"
#include "unet/http/core/response.hpp"
#include "unet/http/middleware.hpp"
#include "unet/http/session.hpp"
#include "unet/http/upgrade_context.hpp"
#include "unet/http/v2/connection.hpp"
#include "unet/http/v2/flow_control.hpp"
#include "unet/http/v2/frame_registry.hpp"
#include "unet/http/v2/settings_registry.hpp"
#include "unet/http/v2/stream.hpp"
#include "unet/http/v2/stream_transport.hpp"
#include "unet/http/v2/wire/flags.hpp"
#include "unet/http/v2/wire/frame_parser.hpp"
#include "unet/http/v2/wire/frame_serializer.hpp"
#include "unet/http/v2/wire/frames.hpp"
#include "unet/http/v2/wire/hpack.hpp"
#include "unet/http/v2/wire/settings.hpp"
#include "unet/http/v2/wire/types.hpp"


namespace usub::unet::http::v2 {

    struct PendingSettingsAck {
        SettingsPayload payload;
        std::chrono::steady_clock::time_point sent_at;
    };


    template<class RouterType>
    class ServerSession : public std::enable_shared_from_this<ServerSession<RouterType>> {
    public:
        using OnStreamFn = std::function<void(Connection &, std::uint32_t /*stream_id*/)>;

        enum class STATE : std::uint8_t {
            PREFACE_WAIT,
            SETTINGS_EXCHANGE,
            OPEN,
            CLOSING,
            CLOSED,
            FAILED,
        };

        ServerSession(std::shared_ptr<RouterType> router, Connection init = {}, bool upgrade_path = false,
                      std::unique_ptr<RequestReader> seeded_request = nullptr,
                      SettingsPayload initial_remote_settings = {})
            : router_(std::move(router)), conn_(std::move(init)), local_(conn_.initial_local_settings),
              on_stream_(router_->getMiddlewareChain().on_http2_stream),
              hpack_decoder_(conn_.initial_local_settings.header_table_size),
              hpack_encoder_(conn_.initial_local_settings.header_table_size) {

            for (const auto &setting: initial_remote_settings.settings) {
                (void) remote_.apply(setting.id, setting.value);
            }

            if (upgrade_path && seeded_request) {
                auto stream = std::make_shared<Stream>();
                stream->id = 1;
                stream->state = Stream::STATE::HALF_CLOSED_REMOTE;
                stream->flow_control.recv_window = static_cast<std::int32_t>(local_.initial_window_size);
                stream->flow_control.send_window = static_cast<std::int32_t>(remote_.initial_window_size);
                stream->request = std::move(*seeded_request);
                stream->request.peer = conn_.peer;
                stream->request_parser.markComplete();
                streams_.emplace(1u, std::move(stream));
                highest_remote_stream_id_ = 1;
            }
        }

        Connection &connection() noexcept { return conn_; }

        std::chrono::milliseconds currentIdleTimeout() const noexcept {
            if (pending_settings_acks_.empty()) return conn_.idle_timeout;
            const auto now = std::chrono::steady_clock::now();
            const auto deadline = pending_settings_acks_.front().sent_at + conn_.settings_ack_timeout;
            if (deadline <= now) return std::chrono::milliseconds{1};
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
            return remaining < conn_.idle_timeout ? remaining : conn_.idle_timeout;
        }

        usub::uvent::task::Awaitable<void> run(std::unique_ptr<usub::unet::core::transport::Transport> transport,
                                               usub::unet::Buffer buffer) {
            return loop(this->shared_from_this(), std::move(transport), std::move(buffer));
        }

    private:
        // Sole caller of transport.send; producers push to outbound_channel_.
        static usub::uvent::task::Awaitable<void>
        writerLoop(std::shared_ptr<ServerSession> self,
                   std::shared_ptr<usub::unet::core::transport::Transport> transport,
                   std::shared_ptr<usub::uvent::sync::AsyncEvent> done) {
            for (;;) {
                auto outbound = co_await self->outbound_channel_.recv();
                if (!outbound) break;
                auto &[chunk] = *outbound;
                if (!chunk.empty()) (void) co_await transport->send(chunk);
            }
            done->set();
            co_return;
        }

        usub::uvent::task::Awaitable<void>
        loop(std::shared_ptr<ServerSession> self,
             std::unique_ptr<usub::unet::core::transport::Transport> transport_unique, usub::unet::Buffer buffer) {
            auto transport = std::shared_ptr<usub::unet::core::transport::Transport>(std::move(transport_unique));
            auto writer_done =
                    std::make_shared<usub::uvent::sync::AsyncEvent>(usub::uvent::sync::Reset::Manual, /*set=*/false);

            const int tid = usub::uvent::sync::detail::current_thread_id();
            usub::uvent::system::co_spawn_static(writerLoop(self, transport, writer_done), tid);

            if (buffer.size() > 0) {
                pending_input_.append(reinterpret_cast<const char *>(buffer.data()), buffer.size());
            }
            // HPACK header_table_size is locked at construction — change via h2_config.
            local_ = conn_.initial_local_settings;
            sendInitialSettingsIfNeeded();

            usub::unet::Buffer read_buf;
            for (;;) {
                if (!pending_settings_acks_.empty()) {
                    const auto now = std::chrono::steady_clock::now();
                    const auto deadline = pending_settings_acks_.front().sent_at + conn_.settings_ack_timeout;
                    if (deadline <= now) {
                        goAway(std::to_underlying(ERROR_CODE::SETTINGS_TIMEOUT), "settings ack timeout");
                        break;
                    }
                }

                bool made_progress = false;

                if (state_ == STATE::PREFACE_WAIT) {
                    if (pending_input_.size() >= h2_preface.size()) {
                        if (std::string_view{pending_input_.data(), h2_preface.size()} != h2_preface) {
                            state_ = STATE::FAILED;
                            break;
                        }
                        pending_input_.erase(0, h2_preface.size());
                        state_ = STATE::SETTINGS_EXCHANGE;
                        made_progress = true;
                    }
                }

                while (state_ != STATE::PREFACE_WAIT && state_ != STATE::FAILED) {
                    auto step = co_await tryDispatchOneFrame(*transport);
                    if (!step) {
                        goAway(std::to_underlying(step.error()), {});
                        goto teardown;
                    }
                    if (*step == 0) break;
                    pending_input_.erase(0, *step);
                    made_progress = true;
                    transport->updateTimeout(conn_.idle_timeout);

                    while (!pending_dispatch_.empty()) {
                        const auto sid = pending_dispatch_.front();
                        pending_dispatch_.pop_front();
                        auto sit = streams_.find(sid);
                        if (sit == streams_.end()) continue;

                        usub::uvent::system::co_spawn_static(dispatchRequest(self, sit->second, transport), tid);
                    }
                }

                if (goaway_pending_) break;

                if (!made_progress) {
                    read_buf.clear();
                    const ssize_t n = co_await transport->read(read_buf);
                    if (n <= 0) break;
                    pending_input_.append(reinterpret_cast<const char *>(read_buf.data()), read_buf.size());
                }
            }

        teardown:
            outbound_channel_.close();
            co_await writer_done->wait();
            co_await transport->shutdown();
            co_return;
        }

    public:
        usub::uvent::task::Awaitable<void> onClose() { co_return; }

    private:
        void sendSettings(std::span<const Setting> updates) {
            SettingsPayload payload{};
            payload.settings.assign(updates.begin(), updates.end());
            pushOutbound(FrameSerializer::serializeSettings(payload, 0));
            pending_settings_acks_.push(PendingSettingsAck{payload, std::chrono::steady_clock::now()});
            local_settings_sent_ = true;
            for (auto &setting: payload.settings) (void) local_.apply(setting.id, setting.value);
        }

        void sendPing(std::uint64_t opaque) { pushOutbound(FrameSerializer::serializePing(opaque, 0)); }

        void goAway(std::uint32_t code, std::string_view debug = {}) {
            const std::uint32_t last = highest_remote_stream_id_;
            pushOutbound(FrameSerializer::serializeGoaway(last, code, debug));
            goaway_last_stream_id_ = last;
            goaway_pending_ = true;
            state_ = STATE::CLOSING;
            send_window_wake_.set();
        }

        void sendFrame(std::uint8_t type, std::uint8_t flags, std::uint32_t stream_id,
                       std::span<const std::byte> payload) {
            FrameHeader h{};
            h.length = static_cast<std::uint32_t>(payload.size());
            h.type = type;
            h.flags = flags;
            h.stream_id = stream_id;
            pushOutbound(FrameSerializer::serializeFrame(h, payload));
        }

        // Full channel → writer is hopelessly behind → GOAWAY.
        void pushOutbound(std::string bytes) {
            if (outbound_channel_.is_closed()) return;
            if (!outbound_channel_.try_send(std::move(bytes))) {
                goaway_pending_ = true;
                state_ = STATE::CLOSING;
                outbound_channel_.close();
            }
        }

        void sendInitialSettingsIfNeeded() {
            if (local_settings_sent_ || state_ == STATE::FAILED) return;
            std::vector<Setting> seed;
            seed.push_back({std::to_underlying(SETTINGS::HEADER_TABLE_SIZE), local_.header_table_size});
            seed.push_back({std::to_underlying(SETTINGS::ENABLE_PUSH), local_.enable_push});
            if (local_.max_concurrent_streams)
                seed.push_back({std::to_underlying(SETTINGS::MAX_CONCURRENT_STREAMS), *local_.max_concurrent_streams});
            seed.push_back({std::to_underlying(SETTINGS::INITIAL_WINDOW_SIZE), local_.initial_window_size});
            seed.push_back({std::to_underlying(SETTINGS::MAX_FRAME_SIZE), local_.max_frame_size});
            if (local_.max_header_list_size)
                seed.push_back({std::to_underlying(SETTINGS::MAX_HEADER_LIST_SIZE), *local_.max_header_list_size});
            if (local_.enable_connect_protocol)
                seed.push_back({std::to_underlying(SETTINGS::ENABLE_CONNECT_PROTOCOL), 1});
            for (const auto &[id, value]: local_.custom) { seed.push_back({id, value}); }
            sendSettings(std::span<const Setting>{seed.data(), seed.size()});
        }

        usub::uvent::task::Awaitable<std::expected<std::size_t, ERROR_CODE>>
        tryDispatchOneFrame(usub::unet::core::transport::Transport &transport) {
            if (pending_input_.size() < frame_header_size) co_return std::size_t{0};

            auto header_span = std::span<const std::byte>{reinterpret_cast<const std::byte *>(pending_input_.data()),
                                                          frame_header_size};
            auto frame_header = FrameParser::parseFrameHeader(header_span);
            if (!frame_header) co_return std::unexpected(frame_header.error());

            if (frame_header->length > local_.max_frame_size) co_return std::unexpected(ERROR_CODE::FRAME_SIZE_ERROR);

            const std::size_t total = frame_header_size + frame_header->length;
            if (pending_input_.size() < total) co_return std::size_t{0};

            if (header_block_stream_ != 0) {
                if (frame_header->type != std::to_underlying(FRAME_TYPE::CONTINUATION) ||
                    frame_header->stream_id != header_block_stream_) {
                    co_return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
                }
            } else if (frame_header->type == std::to_underlying(FRAME_TYPE::CONTINUATION)) {
                co_return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
            }

            auto payload = std::span<const std::byte>{
                    reinterpret_cast<const std::byte *>(pending_input_.data() + frame_header_size),
                    frame_header->length};

            std::expected<void, ERROR_CODE> result{};
            switch (frame_header->type) {
                case std::to_underlying(FRAME_TYPE::DATA):
                    result = co_await onData(*frame_header, payload, transport);
                    break;
                case std::to_underlying(FRAME_TYPE::HEADERS):
                    result = co_await onHeaders(*frame_header, payload, transport);
                    break;
                case std::to_underlying(FRAME_TYPE::CONTINUATION):
                    result = co_await onContinuation(*frame_header, payload, transport);
                    break;
                case std::to_underlying(FRAME_TYPE::SETTINGS):
                    result = co_await onSettings(*frame_header, payload, transport);
                    break;
                case std::to_underlying(FRAME_TYPE::WINDOW_UPDATE):
                    result = co_await onWindowUpdate(*frame_header, payload, transport);
                    break;
                case std::to_underlying(FRAME_TYPE::PING):
                    result = co_await onPing(*frame_header, payload, transport);
                    break;
                case std::to_underlying(FRAME_TYPE::GOAWAY):
                    result = co_await onGoaway(*frame_header, payload, transport);
                    break;
                case std::to_underlying(FRAME_TYPE::RST_STREAM):
                    result = co_await onRstStream(*frame_header, payload, transport);
                    break;
                case std::to_underlying(FRAME_TYPE::PRIORITY):
                    result = co_await onPriority(*frame_header, payload, transport);
                    break;
                case std::to_underlying(FRAME_TYPE::PUSH_PROMISE):
                    // RFC 9113 §8.4 — server must never receive PUSH_PROMISE from a client.
                    result = std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
                    break;
                default: {
                    auto fn = router_->getMiddlewareChain().h2_frame_handlers.find(frame_header->type);
                    if (fn) {
                        auto send_frame_cb = [this](std::uint8_t type, std::uint8_t flags, std::uint32_t sid,
                                                    std::span<const std::byte> payload) {
                            this->sendFrame(type, flags, sid, payload);
                        };
                        result = co_await fn(
                                FrameContext{conn_, *frame_header, payload, transport, std::move(send_frame_cb)});
                    }
                    break;
                }
            }
            if (!result) co_return std::unexpected(result.error());
            co_return total;
        }

        void sendRstStream(std::uint32_t stream_id, ERROR_CODE code) {
            pushOutbound(FrameSerializer::serializeRstStream(stream_id, std::to_underlying(code)));
        }

        void retireStream(std::uint32_t sid) {
            auto it = streams_.find(sid);
            if (it == streams_.end()) return;
            it->second->request.getBodyChannel().close();
            streams_.erase(it);
        }

        usub::uvent::task::Awaitable<bool> enqueueDataFrames(std::uint32_t sid, std::uint32_t max_frame,
                                                             std::string_view bytes, bool end_stream) {
            if (bytes.empty()) {
                if (end_stream) {
                    pushOutbound(FrameSerializer::serializeData(sid, std::span<const std::byte>{},
                                                                static_cast<std::uint8_t>(FLAGS::END_STREAM)));
                }
                co_return true;
            }
            std::size_t off = 0;
            while (off < bytes.size()) {
                auto it = streams_.find(sid);
                if (it == streams_.end()) co_return false;

                std::int32_t stream_credit = it->second->flow_control.send_window;
                std::int32_t conn_credit = conn_flow_.send_window;
                if (stream_credit <= 0 || conn_credit <= 0) {
                    send_window_wake_.reset();
                    if (goaway_pending_) co_return false;
                    co_await send_window_wake_.wait();
                    continue;
                }

                const std::size_t remaining = bytes.size() - off;
                std::size_t take = std::min<std::size_t>(max_frame, remaining);
                take = std::min<std::size_t>(take, static_cast<std::size_t>(stream_credit));
                take = std::min<std::size_t>(take, static_cast<std::size_t>(conn_credit));

                const bool last = (off + take) == bytes.size();
                std::uint8_t flags = 0;
                if (last && end_stream) flags |= FLAGS::END_STREAM;

                pushOutbound(FrameSerializer::serializeData(
                        sid, std::span<const std::byte>{reinterpret_cast<const std::byte *>(bytes.data() + off), take},
                        flags));

                it->second->flow_control.consumeSendCredit(static_cast<std::uint32_t>(take));
                conn_flow_.consumeSendCredit(static_cast<std::uint32_t>(take));
                off += take;
            }
            co_return true;
        }

        usub::uvent::task::Awaitable<bool> enqueueFileData(std::uint32_t sid, std::uint32_t max_frame, int fd,
                                                           std::uint64_t length, std::uint64_t offset,
                                                           bool end_stream) {
            (void) offset;// TODO: honour offset via lseek/_lseeki64.
            constexpr std::size_t kReadChunk = 64u * 1024u;
            std::string buf;
            buf.resize(std::min<std::uint64_t>(kReadChunk, length));
            std::uint64_t remaining = length;
            while (remaining > 0) {
                const std::size_t want = std::min<std::size_t>(buf.size(), remaining);
                const ssize_t got = ::read(fd, buf.data(), want);
                if (got <= 0) co_return false;
                remaining -= static_cast<std::uint64_t>(got);
                const bool last = remaining == 0;
                const bool ok = co_await enqueueDataFrames(sid, max_frame,
                                                           std::string_view{buf.data(), static_cast<std::size_t>(got)},
                                                           /*end_stream=*/end_stream && last);
                if (!ok) co_return false;
            }
            co_return true;
        }

        void bindStreamOps(std::shared_ptr<Stream> stream_ptr) {
            const std::uint32_t sid = stream_ptr->id;
            const std::uint32_t max_frame = remote_.max_frame_size;
            auto self = this->shared_from_this();

            stream_ptr->response.bindOps(ResponseWriter::Ops{
                    .send_body = [self, stream_ptr, sid, max_frame](std::string body) -> Awaitable<bool> {
                        const bool body_empty = body.empty();
                        pushHeadersFrame(self.get(), sid, stream_ptr->response, stream_ptr->response_serializer,
                                         max_frame, /*end_stream=*/body_empty);
                        co_return co_await self->enqueueDataFrames(sid, max_frame, body, /*end_stream=*/true);
                    },
                    .send_file = [self, stream_ptr, sid, max_frame](int fd, std::uint64_t length,
                                                                    std::uint64_t offset) -> Awaitable<bool> {
                        pushHeadersFrame(self.get(), sid, stream_ptr->response, stream_ptr->response_serializer,
                                         max_frame, /*end_stream=*/length == 0);
                        if (length == 0) co_return true;
                        co_return co_await self->enqueueFileData(sid, max_frame, fd, length, offset,
                                                                 /*end_stream=*/true);
                    },
                    .chunk_start = [self, stream_ptr, sid, max_frame]() -> Awaitable<bool> {
                        pushHeadersFrame(self.get(), sid, stream_ptr->response, stream_ptr->response_serializer,
                                         max_frame, /*end_stream=*/false);
                        co_return true;
                    },
                    .chunk_write = [self, sid, max_frame](std::string body) -> Awaitable<bool> {
                        co_return co_await self->enqueueDataFrames(sid, max_frame, body, /*end_stream=*/false);
                    },
                    .chunk_file = [self, sid, max_frame](int fd, std::uint64_t length,
                                                         std::uint64_t offset) -> Awaitable<bool> {
                        if (length == 0) co_return true;
                        co_return co_await self->enqueueFileData(sid, max_frame, fd, length, offset,
                                                                 /*end_stream=*/false);
                    },
                    .chunk_end = [self, sid, max_frame]() -> Awaitable<bool> {
                        // TODO: emit trailers HEADERS(END_STREAM) when non-empty.
                        co_return co_await self->enqueueDataFrames(sid, max_frame, std::string_view{},
                                                                   /*end_stream=*/true);
                    },
            });
        }

        static void pushHeadersFrame(ServerSession *self, std::uint32_t sid, const ResponseWriter &res,
                                     ResponseSerializer &ser, std::uint32_t max_frame, bool end_stream) {
            self->pushOutbound(ser.serialize(sid, res, self->hpack_encoder_, max_frame, end_stream));
        }

        usub::uvent::task::Awaitable<void> finalizeStreamResponse(Stream &stream,
                                                                  usub::unet::core::transport::Transport &transport) {
            switch (stream.response.mode()) {
                case ResponseWriter::Mode::Sent:
                    break;
                case ResponseWriter::Mode::Aborted:
                    sendRstStream(stream.id, ERROR_CODE::CANCEL);
                    break;
                case ResponseWriter::Mode::Empty:
                case ResponseWriter::Mode::Chunked:
                    sendRstStream(stream.id, ERROR_CODE::INTERNAL_ERROR);
                    break;
            }
            streams_.erase(stream.id);
            co_return;
        }

        usub::uvent::task::Awaitable<void>
        dispatchRequest(std::shared_ptr<ServerSession> self, std::shared_ptr<Stream> stream_ptr,
                        std::shared_ptr<usub::unet::core::transport::Transport> transport_ptr) {
            Stream &stream = *stream_ptr;
            usub::unet::core::transport::Transport &transport = *transport_ptr;
            (void) self;

            bindStreamOps(stream_ptr);

            auto match = router_->match(stream.request);

            if (!match) {
                stream.response.metadata.status_code = match.error();
                co_await router_->error(std::to_string(stream.response.metadata.status_code), stream.request,
                                        stream.response);
                co_await finalizeStreamResponse(stream, transport);
                co_return;
            }

            const bool is_upgrade = match->route && match->route->kind == RouteKind::Upgrade;

            auto &chain = router_->getMiddlewareChain();
            if (!co_await chain.execute(MIDDLEWARE_PHASE::HEADER, stream.request, stream.response)) {
                co_await finalizeStreamResponse(stream, transport);
                co_return;
            }
            if (!co_await router_->runRouteMiddleware(MIDDLEWARE_PHASE::HEADER, *match, stream.request,
                                                      stream.response)) {
                co_await finalizeStreamResponse(stream, transport);
                co_return;
            }

            if (is_upgrade) {
                UpgradeContext ctx;
                co_await router_->invokeUpgrade(*match, stream.request, stream.response, ctx);

                if (ctx.accepted && ctx.spawn) {
                    pushOutbound(stream.response_serializer.serialize(stream.id, stream.response, hpack_encoder_,
                                                                      remote_.max_frame_size, /*end_stream=*/false));

                    const std::uint32_t sid = stream.id;
                    const std::uint32_t max_frame = remote_.max_frame_size;
                    auto session_keepalive = this->shared_from_this();
                    StreamTransport::Ops ops{
                            .send_payload = [session_keepalive, sid,
                                             max_frame](std::string bytes) -> Awaitable<ssize_t> {
                                const std::size_t total = bytes.size();
                                if (total == 0) {
                                    session_keepalive->pushOutbound(
                                            FrameSerializer::serializeData(sid, std::span<const std::byte>{},
                                                                           /*flags=*/0));
                                    co_return 0;
                                }
                                const bool ok = co_await session_keepalive->enqueueDataFrames(
                                        sid, max_frame, std::string_view{bytes.data(), total},
                                        /*end_stream=*/false);
                                co_return ok ? static_cast<ssize_t>(total) : -1;
                            },
                            .close_stream = [session_keepalive, sid]() -> Awaitable<void> {
                                session_keepalive->sendRstStream(sid, ERROR_CODE::CANCEL);
                                session_keepalive->retireStream(sid);
                                co_return;
                            },
                    };

                    // StreamTransport reads from the same channel the handler could have read
                    // from — bytes the handler didn't consume before ctx.accept are still there.
                    auto stream_transport =
                            std::make_unique<StreamTransport>(std::move(ops), stream.request.getBodyChannelPtr());
                    stream.tunnel = true;

                    usub::uvent::system::co_spawn(ctx.spawn(std::move(stream_transport), usub::unet::Buffer{}));
                    co_return;
                }
            } else {
                co_await router_->invoke(*match, stream.request, stream.response);
            }

            co_await finalizeStreamResponse(stream, transport);
            co_return;
        }

        usub::uvent::task::Awaitable<std::expected<void, ERROR_CODE>>
        onSettings(FrameHeader header, std::span<const std::byte> payload,
                   usub::unet::core::transport::Transport &transport) {
            if (header.flags & FLAGS::ACK) {
                if (header.length != 0) co_return std::unexpected(ERROR_CODE::FRAME_SIZE_ERROR);
                if (pending_settings_acks_.empty()) co_return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
                pending_settings_acks_.pop();
                co_return std::expected<void, ERROR_CODE>{};
            }

            auto parsed = FrameParser::parseSettings(header, payload);
            if (!parsed) co_return std::unexpected(parsed.error());

            for (auto &one: parsed->settings) {
                if (one.id >= 0x01 && one.id <= 0x06) {
                    // RFC 9113 §6.9.2 — SETTINGS_INITIAL_WINDOW_SIZE changes apply retroactively
                    // to every stream's send window as a signed delta.
                    if (one.id == std::to_underlying(SETTINGS::INITIAL_WINDOW_SIZE)) {
                        const std::int64_t old_iws = static_cast<std::int64_t>(remote_.initial_window_size);
                        if (!remote_.apply(one.id, one.value))
                            co_return std::unexpected(ERROR_CODE::FLOW_CONTROL_ERROR);
                        const std::int64_t delta = static_cast<std::int64_t>(one.value) - old_iws;
                        if (delta != 0) {
                            for (auto &kv: streams_) {
                                auto &flow_control = kv.second->flow_control;
                                const std::int64_t new_window =
                                        static_cast<std::int64_t>(flow_control.send_window) + delta;
                                if (new_window > 0x7fffffffLL)
                                    co_return std::unexpected(ERROR_CODE::FLOW_CONTROL_ERROR);
                                flow_control.send_window = static_cast<std::int32_t>(new_window);
                            }
                            send_window_wake_.set();
                        }
                        continue;
                    }
                    if (!remote_.apply(one.id, one.value)) { co_return std::unexpected(ERROR_CODE::PROTOCOL_ERROR); }
                    continue;
                }
                if (one.id >= 0x07 && one.id <= 0x0a) continue;
                if (!router_->getMiddlewareChain().h2_setting_handlers.dispatch(conn_, one.id, one.value)) {
                    remote_.custom[one.id] = one.value;
                }
            }

            pushOutbound(FrameSerializer::serializeSettingsAck());
            peer_settings_acked_ = true;
            if (state_ == STATE::SETTINGS_EXCHANGE && peer_settings_acked_) {
                state_ = STATE::OPEN;
                for (auto &kv: streams_) {
                    if (kv.second->request_parser.isDone() && kv.second->state == Stream::STATE::HALF_CLOSED_REMOTE) {
                        pending_dispatch_.push_back(kv.first);
                    }
                }
            }
            co_return std::expected<void, ERROR_CODE>{};
        }

        usub::uvent::task::Awaitable<std::expected<void, ERROR_CODE>>
        onWindowUpdate(FrameHeader header, std::span<const std::byte> payload,
                       usub::unet::core::transport::Transport &) {
            auto parsed = FrameParser::parseWindowUpdate(header, payload);
            if (!parsed) co_return std::unexpected(parsed.error());

            if (header.stream_id == 0) {
                if (!conn_flow_.applySendWindowUpdate(parsed->window_size_increment))
                    co_return std::unexpected(ERROR_CODE::FLOW_CONTROL_ERROR);
            } else {
                auto it = streams_.find(header.stream_id);
                if (it == streams_.end()) {
                    // RFC 9113 §5.1 — WINDOW_UPDATE on idle stream = connection PROTOCOL_ERROR.
                    if (header.stream_id > highest_remote_stream_id_)
                        co_return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
                } else if (!it->second->flow_control.applySendWindowUpdate(parsed->window_size_increment)) {
                    // RFC 9113 §6.9 — stream-level FLOW_CONTROL_ERROR.
                    sendRstStream(header.stream_id, ERROR_CODE::FLOW_CONTROL_ERROR);
                    retireStream(header.stream_id);
                    co_return std::expected<void, ERROR_CODE>{};
                }
            }
            send_window_wake_.set();
            co_return std::expected<void, ERROR_CODE>{};
        }

        usub::uvent::task::Awaitable<std::expected<void, ERROR_CODE>>
        onPing(FrameHeader header, std::span<const std::byte> payload,
               usub::unet::core::transport::Transport &transport) {
            auto parsed = FrameParser::parsePing(header, payload);
            if (!parsed) co_return std::unexpected(parsed.error());
            if (!(header.flags & FLAGS::ACK)) { pushOutbound(FrameSerializer::serializePingAck(parsed->opaque_data)); }
            co_return std::expected<void, ERROR_CODE>{};
        }

        usub::uvent::task::Awaitable<std::expected<void, ERROR_CODE>>
        onGoaway(FrameHeader header, std::span<const std::byte> payload, usub::unet::core::transport::Transport &) {
            auto parsed = FrameParser::parseGoaway(header, payload);
            if (!parsed) co_return std::unexpected(parsed.error());
            state_ = STATE::CLOSING;
            co_return std::expected<void, ERROR_CODE>{};
        }

        usub::uvent::task::Awaitable<std::expected<void, ERROR_CODE>>
        onRstStream(FrameHeader header, std::span<const std::byte> payload, usub::unet::core::transport::Transport &) {
            auto parsed = FrameParser::parseRstStream(header, payload);
            if (!parsed) co_return std::unexpected(parsed.error());
            // RFC 9113 §6.4.
            if (header.stream_id == 0) co_return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
            if (streams_.contains(header.stream_id)) {
                retireStream(header.stream_id);
            } else if (header.stream_id > highest_remote_stream_id_) {
                co_return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
            }
            co_return std::expected<void, ERROR_CODE>{};
        }

        usub::uvent::task::Awaitable<std::expected<void, ERROR_CODE>>
        onPriority(FrameHeader header, std::span<const std::byte> payload, usub::unet::core::transport::Transport &) {
            auto parsed = FrameParser::parsePriority(header, payload);
            if (!parsed) co_return std::unexpected(parsed.error());
            // RFC 9113 §5.3.1 — a stream MUST NOT depend on itself.
            if (parsed->stream_dependency == header.stream_id) {
                sendRstStream(header.stream_id, ERROR_CODE::PROTOCOL_ERROR);
            }
            co_return std::expected<void, ERROR_CODE>{};
        }

        usub::uvent::task::Awaitable<std::expected<void, ERROR_CODE>>
        onHeaders(FrameHeader header, std::span<const std::byte> payload, usub::unet::core::transport::Transport &) {
            if (header.stream_id == 0 || (header.stream_id & 1u) == 0)
                co_return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
            if (header.stream_id <= highest_remote_stream_id_ && !streams_.contains(header.stream_id))
                co_return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);

            auto parsed = FrameParser::parseHeaders(header, payload);
            if (!parsed) co_return std::unexpected(parsed.error());

            // RFC 9113 §5.3.1 — self-referential priority is a stream PROTOCOL_ERROR.
            if (parsed->has_priority && parsed->stream_dependency == header.stream_id) {
                sendRstStream(header.stream_id, ERROR_CODE::PROTOCOL_ERROR);
                co_return std::expected<void, ERROR_CODE>{};
            }

            // RFC 9113 §5.1.2 — enforce SETTINGS_MAX_CONCURRENT_STREAMS.
            const bool is_new_stream = !streams_.contains(header.stream_id);
            if (is_new_stream && local_.max_concurrent_streams.has_value() &&
                streams_.size() >= *local_.max_concurrent_streams) {
                sendRstStream(header.stream_id, ERROR_CODE::REFUSED_STREAM);
                if (header.stream_id > highest_remote_stream_id_) highest_remote_stream_id_ = header.stream_id;
                co_return std::expected<void, ERROR_CODE>{};
            }

            auto [it, inserted] = streams_.try_emplace(header.stream_id, std::make_shared<Stream>());
            Stream &stream = *it->second;
            if (inserted) {
                stream.id = header.stream_id;
                stream.state = Stream::STATE::OPEN;
                stream.flow_control.recv_window = static_cast<std::int32_t>(local_.initial_window_size);
                stream.flow_control.send_window = static_cast<std::int32_t>(remote_.initial_window_size);
                stream.request.peer = conn_.peer;
                if (header.stream_id > highest_remote_stream_id_) highest_remote_stream_id_ = header.stream_id;

                if (on_stream_) on_stream_(conn_, header.stream_id);
            } else {
                // RFC 9113 §5.1.
                if (stream.state == Stream::STATE::HALF_CLOSED_REMOTE || stream.state == Stream::STATE::CLOSED) {
                    sendRstStream(header.stream_id, ERROR_CODE::STREAM_CLOSED);
                    retireStream(header.stream_id);
                    co_return std::expected<void, ERROR_CODE>{};
                }
            }

            stream.continuation_count = 0;
            stream.request_parser.appendHeaderFragment(parsed->header_block_fragment);
            if (header.flags & FLAGS::END_STREAM) stream.request_parser.markEndStream();

            const bool end_headers = (header.flags & FLAGS::END_HEADERS) != 0;
            if (!end_headers) {
                header_block_stream_ = header.stream_id;
                co_return std::expected<void, ERROR_CODE>{};
            }
            header_block_stream_ = 0;

            // RFC 9113 §8.1 — second HEADERS block on a stream is trailers; MUST carry END_STREAM.
            if (stream.request_parser.getContext().state != RequestParser::STATE::HEADERS) {
                if (!(header.flags & FLAGS::END_STREAM)) co_return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
                stream.state = Stream::STATE::HALF_CLOSED_REMOTE;
                stream.request.getBodyChannel().close();
                if (!(header.flags & FLAGS::END_HEADERS)) {
                    header_block_stream_ = header.stream_id;
                    co_return std::expected<void, ERROR_CODE>{};
                }
                auto trailer_result = stream.request_parser.decodeTrailers(hpack_decoder_, stream.request);
                if (!trailer_result) co_return std::unexpected(trailer_result.error());
                co_return std::expected<void, ERROR_CODE>{};
            }

            auto decoded = stream.request_parser.decodeHeaders(hpack_decoder_, stream.request);
            if (!decoded) co_return std::unexpected(decoded.error());
            stream.tunnel = *decoded;

            if (stream.request_parser.isDone()) stream.state = Stream::STATE::HALF_CLOSED_REMOTE;
            pending_dispatch_.push_back(header.stream_id);
            if (stream.request_parser.isDone()) {
                if (stream.request_parser.getContext().expected_body_bytes.has_value() &&
                    *stream.request_parser.getContext().expected_body_bytes != 0) {
                    sendRstStream(header.stream_id, ERROR_CODE::PROTOCOL_ERROR);
                    retireStream(header.stream_id);
                    co_return std::expected<void, ERROR_CODE>{};
                }
                stream.request.getBodyChannel().close();
            }
            co_return std::expected<void, ERROR_CODE>{};
        }

        usub::uvent::task::Awaitable<std::expected<void, ERROR_CODE>>
        onContinuation(FrameHeader header, std::span<const std::byte> payload,
                       usub::unet::core::transport::Transport &) {
            auto parsed = FrameParser::parseContinuation(header, payload);
            if (!parsed) co_return std::unexpected(parsed.error());

            auto it = streams_.find(header.stream_id);
            if (it == streams_.end()) co_return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
            Stream &stream = *it->second;

            // CONTINUATION-storm defense (RFC 9113 §10.5.1).
            if (++stream.continuation_count > conn_.max_continuations_per_header_block)
                co_return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);

            const bool is_trailer_block = stream.request_parser.getContext().state == RequestParser::STATE::TRAILERS;
            stream.request_parser.appendHeaderFragment(parsed->header_block_fragment);

            if (!(header.flags & FLAGS::END_HEADERS)) co_return std::expected<void, ERROR_CODE>{};
            header_block_stream_ = 0;

            if (is_trailer_block) {
                auto trailer_result = stream.request_parser.decodeTrailers(hpack_decoder_, stream.request);
                if (!trailer_result) co_return std::unexpected(trailer_result.error());
                stream.state = Stream::STATE::HALF_CLOSED_REMOTE;
                stream.request.getBodyChannel().close();
                co_return std::expected<void, ERROR_CODE>{};
            }

            auto decoded = stream.request_parser.decodeHeaders(hpack_decoder_, stream.request);
            if (!decoded) co_return std::unexpected(decoded.error());
            stream.tunnel = *decoded;

            if (stream.request_parser.isDone()) stream.state = Stream::STATE::HALF_CLOSED_REMOTE;
            pending_dispatch_.push_back(header.stream_id);
            if (stream.request_parser.isDone()) {
                if (stream.request_parser.getContext().expected_body_bytes.has_value() &&
                    *stream.request_parser.getContext().expected_body_bytes != 0) {
                    sendRstStream(header.stream_id, ERROR_CODE::PROTOCOL_ERROR);
                    retireStream(header.stream_id);
                    co_return std::expected<void, ERROR_CODE>{};
                }
                stream.request.getBodyChannel().close();
            }
            co_return std::expected<void, ERROR_CODE>{};
        }

        usub::uvent::task::Awaitable<std::expected<void, ERROR_CODE>>
        onData(FrameHeader header, std::span<const std::byte> payload,
               usub::unet::core::transport::Transport &transport) {
            auto parsed = FrameParser::parseData(header, payload);
            if (!parsed) co_return std::unexpected(parsed.error());
            if (header.stream_id == 0) co_return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);

            auto it = streams_.find(header.stream_id);
            if (it == streams_.end()) {
                // RFC 9113 §5.1.
                if (header.stream_id > highest_remote_stream_id_) co_return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
                sendRstStream(header.stream_id, ERROR_CODE::STREAM_CLOSED);
                co_return std::expected<void, ERROR_CODE>{};
            }
            Stream &stream = *it->second;

            if (stream.state != Stream::STATE::OPEN && stream.state != Stream::STATE::HALF_CLOSED_LOCAL) {
                sendRstStream(header.stream_id, ERROR_CODE::STREAM_CLOSED);
                retireStream(header.stream_id);
                co_return std::expected<void, ERROR_CODE>{};
            }

            const std::uint32_t consumed = static_cast<std::uint32_t>(payload.size());
            conn_flow_.consumeRecvCredit(consumed);
            stream.flow_control.consumeRecvCredit(consumed);

            // RFC 9113 §8.1.2.6.
            stream.request_parser.getContext().body_bytes_received += parsed->data.size();
            if (stream.request_parser.getContext().expected_body_bytes.has_value()) {
                const auto expected = *stream.request_parser.getContext().expected_body_bytes;
                const auto received = stream.request_parser.getContext().body_bytes_received;
                if (received > expected || ((header.flags & FLAGS::END_STREAM) && received != expected)) {
                    sendRstStream(header.stream_id, ERROR_CODE::PROTOCOL_ERROR);
                    retireStream(header.stream_id);
                    co_return std::expected<void, ERROR_CODE>{};
                }
            }

            if (!parsed->data.empty()) {
                std::string chunk(reinterpret_cast<const char *>(parsed->data.data()), parsed->data.size());
                (void) co_await stream.request.getBodyChannel().push(std::move(chunk));
            }

            if (consumed > 0) {
                pushOutbound(FrameSerializer::serializeWindowUpdate(0, consumed));
                pushOutbound(FrameSerializer::serializeWindowUpdate(header.stream_id, consumed));
            }

            if (header.flags & FLAGS::END_STREAM) {
                stream.state = Stream::STATE::HALF_CLOSED_REMOTE;
                stream.request.getBodyChannel().close();
            }
            co_return std::expected<void, ERROR_CODE>{};
        }

        std::shared_ptr<RouterType> router_;
        Connection conn_{};

        STATE state_{STATE::PREFACE_WAIT};
        Settings local_;
        Settings remote_;
        FlowControl conn_flow_;

        std::string pending_input_;

        std::unordered_map<std::uint32_t, std::shared_ptr<Stream>> streams_;
        std::uint32_t highest_remote_stream_id_{0};
        std::uint32_t header_block_stream_{0};
        std::uint32_t goaway_last_stream_id_{0};

        bool local_settings_sent_{false};
        bool peer_settings_acked_{false};
        std::queue<PendingSettingsAck> pending_settings_acks_;

        OnStreamFn on_stream_;

        HpackDecoder hpack_decoder_;
        HpackEncoder hpack_encoder_;

        usub::uvent::sync::AsyncChannel<std::string> outbound_channel_{2048};
        std::deque<std::uint32_t> pending_dispatch_;
        bool goaway_pending_{false};

        usub::uvent::sync::AsyncEvent send_window_wake_{usub::uvent::sync::Reset::Manual, /*set=*/false};
    };

}// namespace usub::unet::http::v2
