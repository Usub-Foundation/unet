#pragma once

#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <uvent/sync/AsyncEvent.h>
#include <uvent/system/SystemContext.h>

#include "unet/core/io_provider.hpp"
#include "unet/core/transport/transport.hpp"
#include "unet/header/keep_alive.hpp"
#include "unet/http/core/request.hpp"
#include "unet/http/core/response.hpp"
#include "unet/http/session.hpp"
#include "unet/http/upgrade_context.hpp"
#include "unet/http/v1/connection.hpp"
#include "unet/http/v1/wire/request_parser.hpp"
#include "unet/http/v1/wire/response_serializer.hpp"
#include "unet/http/v2/upgrade.hpp"

namespace usub::unet::http::v1 {

    using core::transport::Transport;
    using usub::uvent::task::Awaitable;

    template<typename RouterType>
    class ServerSession : public std::enable_shared_from_this<ServerSession<RouterType>> {
    public:
        explicit ServerSession(std::shared_ptr<RouterType> router, Connection init = {})
            : router_(std::move(router)), conn_(std::move(init)) {}

        Awaitable<void> run(std::unique_ptr<Transport> transport, Buffer buffer) {
            return loop(this->shared_from_this(), std::move(transport), std::move(buffer));
        }

    private:
        struct TransportCloser {
            Transport *transport{nullptr};
            void release() noexcept { transport = nullptr; }
            ~TransportCloser() {
                if (transport) transport->close();
            }
        };

        Awaitable<void> loop(std::shared_ptr<ServerSession> self, std::unique_ptr<Transport> transport, Buffer buffer) {
            TransportCloser closer{transport.get()};

            std::string_view buffer_view{reinterpret_cast<const char *>(buffer.data()), buffer.size()};
            std::string_view::const_iterator begin = buffer_view.begin();
            std::string_view::const_iterator end = buffer_view.end();

            auto fillIfEmpty = [&]() -> Awaitable<ssize_t> {
                if (begin != end) co_return static_cast<ssize_t>(end - begin);

                buffer.clear();
                const ssize_t n = co_await transport->read(buffer);
                if (n <= 0) co_return n;

                buffer_view = std::string_view{reinterpret_cast<const char *>(buffer.data()), buffer.size()};
                begin = buffer_view.begin();
                end = buffer_view.end();
                co_return static_cast<ssize_t>(end - begin);
            };

            // STATE lifecycle{STATE::INITIAL};

            if (auto &cb = this->router_->getMiddlewareChain().on_http1_connection; cb) { cb(this->conn_); }

            this->keep_alive_deadline_ = this->conn_.default_keep_alive_timeout;

            for (;;) {
                RequestReader request{};
                request.peer = this->conn_.peer;
                ResponseWriter response{};
                v1::RequestParser request_parser{};

                v1::ResponseSerializer streaming_serializer{[&transport](std::string bytes) -> Awaitable<bool> {
                    co_await transport->send(bytes);
                    co_return true;
                }};

                response.bindOps(ResponseWriter::Ops{
                        .send_body = [&streaming_serializer, &response](std::string s) -> Awaitable<bool> {
                            co_return co_await streaming_serializer.sendBody(response, std::move(s));
                        },
                        .send_file = [&streaming_serializer, &response,
                                      &transport](int fd, std::uint64_t len, std::uint64_t off) -> Awaitable<bool> {
                            if (!co_await streaming_serializer.sendBodyHead(response, len)) co_return false;
                            co_return co_await transport->sendFile(fd, len, off);
                        },
                        .chunk_start = [&streaming_serializer, &response]() -> Awaitable<bool> {
                            co_return co_await streaming_serializer.sendHeaders(response);
                        },
                        .chunk_write = [&streaming_serializer, &response](std::string s) -> Awaitable<bool> {
                            const auto *p = reinterpret_cast<const std::byte *>(s.data());
                            co_return co_await streaming_serializer.writeChunk(response,
                                                                               std::span<const std::byte>{p, s.size()});
                        },
                        .chunk_file = [&streaming_serializer, &transport](int fd, std::uint64_t len,
                                                                          std::uint64_t off) -> Awaitable<bool> {
                            if (!co_await streaming_serializer.writeChunkHeader(static_cast<std::size_t>(len)))
                                co_return false;
                            if (!co_await transport->sendFile(fd, len, off)) co_return false;
                            co_return co_await streaming_serializer.writeChunkTrailer();
                        },
                        .chunk_end = [&streaming_serializer, &response]() -> Awaitable<bool> {
                            co_return co_await streaming_serializer.end(response);
                        },
                });

                usub::uvent::sync::AsyncEvent handler_done{usub::uvent::sync::Reset::Manual, false};
                auto &state = request_parser.getContext().state;
                this->current_match_.reset();

                transport->updateTimeout(this->keep_alive_deadline_);
                bool got_first_byte = false;
                bool close_after_error = false;

                for (;;) {
                    const ssize_t rdsz = co_await fillIfEmpty();
                    if (rdsz <= 0) co_return;
                    if (!got_first_byte) {
                        got_first_byte = true;
                        transport->updateTimeout(this->conn_.header_timeout);
                    }

                    auto parse_result = request_parser.step(request, begin, end);
                    if (!parse_result) {
                        response.metadata.status_code = parse_result.error().expected_status;
                        co_await this->router_->error(std::to_string(response.metadata.status_code), request, response);
                        close_after_error = true;// parser state unreliable — must close
                        goto send_error;
                    }

                    if (state == v1::RequestParser::STATE::HEADERS_DONE) {
                        response.metadata.version = request.metadata.version;
                        this->negotiateKeepAlive(request, response);

                        // RFC 7540 §3.2 h2c — takes precedence over user routes.
                        if (auto &h2c = this->router_->getMiddlewareChain().h2c_upgrade; h2c) {
                            if (auto settings = detectH2cUpgrade(request)) {
                                co_await transport->send(v2::upgrade_response);
                                Buffer carry;
                                if (begin != end) {
                                    const auto remaining = static_cast<std::size_t>(end - begin);
                                    std::uint8_t *dst = carry.append_raw(remaining);
                                    std::memcpy(dst, &*begin, remaining);
                                }
                                closer.release();
                                usub::uvent::system::co_spawn(h2c(std::move(transport), std::move(carry),
                                                                  std::move(request), std::move(*settings)));
                                co_return;
                            }
                        }

                        auto match = this->router_->match(request);
                        if (!match) {
                            request_parser.getContext().state = v1::RequestParser::STATE::FAILED;
                            response.metadata.status_code = match.error();
                            // co_await this->router_->error("log", request, response);
                            co_await this->router_->error(std::to_string(response.metadata.status_code), request,
                                                          response);
                            goto send_error;
                        }
                        this->current_match_ = std::move(match.value());
                        break;
                    }
                }

                if (!co_await this->router_->getMiddlewareChain().execute(MIDDLEWARE_PHASE::HEADER, request,
                                                                          response)) {
                    co_return;
                }

                {
                    const bool is_upgrade =
                            this->current_match_->route && this->current_match_->route->kind == RouteKind::Upgrade;
                    UpgradeContext upgrade_ctx;

                    this->consumer_closed_ = false;
                    usub::uvent::system::co_spawn(
                            this->dispatchHandler(self, request, response, upgrade_ctx, is_upgrade, handler_done));

                    // TODO: min-throughput window over the body phase.
                    for (;;) {
                        if (!request_parser.step(request, begin, end)) {
                            request.getBodyChannel().signalError(BODY_ERROR::PARSER_ERROR);
                            break;
                        }

                        if (auto chunk = request_parser.takePendingBody(); !chunk.empty() && !this->consumer_closed_) {
                            if (!co_await request.getBodyChannel().push(std::move(chunk)))
                                this->consumer_closed_ = true;
                        }

                        if (state == v1::RequestParser::STATE::COMPLETE) {
                            request.getBodyChannel().close();
                            break;
                        }

                        transport->updateTimeout(this->conn_.idle_body_timeout);
                        const ssize_t rdsz = co_await fillIfEmpty();
                        if (rdsz <= 0) {
                            request.getBodyChannel().signalError(BODY_ERROR::PROTOCOL_ERROR);
                            break;
                        }
                    }

                    if (!request.getBodyChannel().isClosed()) request.getBodyChannel().close();
                    co_await handler_done.wait();

                    if (is_upgrade && upgrade_ctx.accepted && upgrade_ctx.spawn) {
                        co_await transport->send(v1::ResponseSerializer::serialize(response));

                        Buffer carry;
                        if (begin != end) {
                            const auto remaining = static_cast<std::size_t>(end - begin);
                            std::uint8_t *dst = carry.append_raw(remaining);
                            std::memcpy(dst, &*begin, remaining);
                        }

                        closer.release();
                        usub::uvent::system::co_spawn(upgrade_ctx.spawn(std::move(transport), std::move(carry)));
                        co_return;
                    }

                    if (response.mode() != ResponseWriter::Mode::Sent) co_return;
                }

                ++this->conn_.requests_served;
                if (!this->shouldKeepAlive()) co_return;
                continue;

            send_error:
                if (response.mode() == ResponseWriter::Mode::Empty) { co_await response.send(std::string{}); }
                if (close_after_error) co_return;

                while (state != v1::RequestParser::STATE::COMPLETE && state != v1::RequestParser::STATE::FAILED) {
                    if (!request_parser.step(request, begin, end)) { co_return; }
                    if (state == v1::RequestParser::STATE::COMPLETE) break;
                    // transport->updateTimeout(this->conn_.idle_body_timeout);
                    const ssize_t rdsz = co_await fillIfEmpty();
                    if (rdsz <= 0) co_return;
                }

                ++this->conn_.requests_served;
                if (!this->shouldKeepAlive()) co_return;
                continue;
            }
        }

        static std::optional<v2::SettingsPayload> detectH2cUpgrade(const RequestReader &req) {
            const auto upgrade = req.headers.value("upgrade");
            const auto connection = req.headers.value("connection");
            const auto settings_h = req.headers.value("http2-settings");
            if (!upgrade || !connection || !settings_h) return std::nullopt;
            if (!ieq(*upgrade, "h2c")) return std::nullopt;
            if (!tokenListContains(*connection, "upgrade") || !tokenListContains(*connection, "http2-settings"))
                return std::nullopt;
            return v2::parseSettingsHeader(*settings_h);
        }

        static bool ieq(std::string_view a, std::string_view b) noexcept {
            if (a.size() != b.size()) return false;
            for (std::size_t i = 0; i < a.size(); ++i) {
                char x = a[i];
                char y = b[i];
                if (x >= 'A' && x <= 'Z') x = static_cast<char>(x + 32);
                if (y >= 'A' && y <= 'Z') y = static_cast<char>(y + 32);
                if (x != y) return false;
            }
            return true;
        }

        static bool tokenListContains(std::string_view list, std::string_view tok) noexcept {
            std::size_t i = 0;
            while (i < list.size()) {
                while (i < list.size() && (list[i] == ' ' || list[i] == '\t' || list[i] == ',')) ++i;
                std::size_t j = i;
                while (j < list.size() && list[j] != ',') ++j;
                std::size_t end = j;
                while (end > i && (list[end - 1] == ' ' || list[end - 1] == '\t')) --end;
                if (ieq(list.substr(i, end - i), tok)) return true;
                i = j + 1;
            }
            return false;
        }

        bool shouldKeepAlive() const noexcept { return this->kept_alive_decision_; }

        void negotiateKeepAlive(const RequestReader &request, ResponseWriter &response) {
            bool keep = !this->conn_.close_after_cycle && this->conn_.keep_alive_enabled;
            if (keep && this->conn_.keep_alive_max_requests > 0 &&
                this->conn_.requests_served + 1 >= this->conn_.keep_alive_max_requests) {
                keep = false;
            }
            if (keep && !clientWantsKeepAlive(request)) keep = false;

            auto timeout = this->conn_.default_keep_alive_timeout;
            if (keep && this->conn_.honour_keep_alive) {
                if (auto h = request.headers.value("keep-alive"); h) {
                    auto hint = usub::unet::header::parseKeepAlive(*h);
                    if (hint.timeout) {
                        auto requested = std::chrono::duration_cast<std::chrono::milliseconds>(*hint.timeout);
                        timeout = std::min(requested, this->conn_.max_keep_alive_timeout);
                    }
                }
            }
            if (timeout > this->conn_.max_keep_alive_timeout) timeout = this->conn_.max_keep_alive_timeout;

            this->kept_alive_decision_ = keep;
            this->keep_alive_deadline_ = keep ? timeout : std::chrono::milliseconds{0};

            if (keep) {
                response.headers.addHeader(std::string_view{"connection"}, std::string_view{"keep-alive"});
                const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(timeout).count();
                std::string ka_value = "timeout=" + std::to_string(seconds);
                if (this->conn_.keep_alive_max_requests > 0) {
                    const std::uint32_t remaining =
                            this->conn_.keep_alive_max_requests - (this->conn_.requests_served + 1);
                    ka_value += ", max=";
                    ka_value += std::to_string(remaining);
                }
                response.headers.addHeader(std::string_view{"keep-alive"}, std::move(ka_value));
            } else {
                response.headers.addHeader(std::string_view{"connection"}, std::string_view{"close"});
            }
        }

        static bool clientWantsKeepAlive(const RequestReader &request) noexcept {
            bool close = (request.metadata.version == VERSION::HTTP_1_0);
            if (auto c = request.headers.value("connection"); c) {
                if (tokenListContains(*c, "close")) close = true;
                else if (tokenListContains(*c, "keep-alive"))
                    close = false;
            }
            return !close;
        }

        Awaitable<void> dispatchHandler(std::shared_ptr<ServerSession> self, RequestReader &request,
                                        ResponseWriter &response, UpgradeContext &upgrade_ctx, bool is_upgrade,
                                        usub::uvent::sync::AsyncEvent &handler_done) {
            if (is_upgrade) {
                co_await this->router_->invokeUpgrade(*this->current_match_, request, response, upgrade_ctx);
            } else {
                co_await this->router_->invoke(*this->current_match_, request, response);
            }
            request.getBodyChannel().close();
            handler_done.set();
            co_return;
        }

        // // state of loop
        // enum class STATE {
        //     INITIAL,
        //     HEADERS,
        //     COMPLETE,
        //     ERROR_TRY_DRAIn,
        // };

        std::shared_ptr<RouterType> router_;
        std::optional<typename RouterType::MatchResult> current_match_{};
        Connection conn_{};
        bool consumer_closed_{false};

        bool kept_alive_decision_{true};
        std::chrono::milliseconds keep_alive_deadline_{0};
    };

}// namespace usub::unet::http::v1
