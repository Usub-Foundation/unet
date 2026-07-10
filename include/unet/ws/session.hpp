#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <uvent/Uvent.h>
#include <uvent/system/SystemContext.h>

#include "unet/core/io_provider.hpp"
#include "unet/core/transport/transport.hpp"
#include "unet/ws/core/frame.hpp"
#include "unet/ws/handler.hpp"
#include "unet/ws/reader.hpp"
#include "unet/ws/wire/frame_parser.hpp"
#include "unet/ws/writer.hpp"

namespace usub::unet::ws {

    namespace detail {
        inline std::atomic<std::uint64_t> &nextConnectionId() noexcept {
            static std::atomic<std::uint64_t> id{1};
            return id;
        }

        inline void unmaskInPlace(std::string &chunk, const std::array<std::uint8_t, 4> &key,
                                  std::size_t index_in_frame) noexcept {
            for (std::size_t i = 0; i < chunk.size(); ++i) {
                chunk[i] = static_cast<char>(static_cast<std::uint8_t>(chunk[i]) ^ key[(index_in_frame + i) % 4]);
            }
        }
    }// namespace detail

    template<typename HandlerT>
    class Session : public std::enable_shared_from_this<Session<HandlerT>> {
    public:
        explicit Session(std::shared_ptr<HandlerT> handler)
            : handler_(std::move(handler)), write_state_(std::make_shared<WriteState>()) {}

        usub::uvent::task::Awaitable<void> run(std::unique_ptr<usub::unet::core::transport::Transport> transport,
                                               usub::unet::Buffer buffer) {
            return loop(this->shared_from_this(), std::move(transport), std::move(buffer));
        }

    private:
        usub::uvent::task::Awaitable<void> loop(std::shared_ptr<Session> self,
                                                std::unique_ptr<usub::unet::core::transport::Transport> transport,
                                                usub::unet::Buffer buffer) {

            std::shared_ptr<usub::unet::core::transport::Transport> tr{std::move(transport)};
            const std::uint64_t conn_id = detail::nextConnectionId().fetch_add(1, std::memory_order_relaxed);
            {
                std::scoped_lock lk(this->write_state_->mu);
                this->write_state_->transport = tr;
                this->write_state_->id = conn_id;
            }
            ServerWriter writer{this->write_state_};

            tr->updateTimeout(std::chrono::milliseconds{this->handler_->timeout_ms});
            usub::uvent::system::co_spawn(this->handler_->onOpen(writer));

            std::string_view buffer_view{reinterpret_cast<const char *>(buffer.data()), buffer.size()};
            std::string_view::const_iterator begin = buffer_view.begin();
            std::string_view::const_iterator end = buffer_view.end();

            auto fillIfEmpty = [&]() -> usub::uvent::task::Awaitable<bool> {
                if (begin != end) co_return true;
                buffer.clear();
                const ssize_t n = co_await tr->read(buffer);
                if (n <= 0) co_return false;
                tr->updateTimeout(std::chrono::milliseconds{this->handler_->timeout_ms});
                buffer_view = std::string_view{reinterpret_cast<const char *>(buffer.data()), buffer.size()};
                begin = buffer_view.begin();
                end = buffer_view.end();
                co_return true;
            };

            auto closeInFlight = [&]() {
                if (this->in_flight_) {
                    this->in_flight_->close();
                    this->in_flight_.reset();
                }
            };

            for (;;) {
                FrameHeader header{};
                for (;;) {
                    if (!co_await fillIfEmpty()) {
                        closeInFlight();
                        co_await this->handler_->onClose(writer);
                        co_await tr->shutdown();
                        co_return;
                    }
                    auto r = this->parser_.stepHeader(header, begin, end);
                    if (!r) {
                        co_await writer.close(CLOSE_CODE::PROTOCOL_ERROR, r.error().message);
                        closeInFlight();
                        co_await this->handler_->onClose(writer);
                        co_await tr->shutdown();
                        co_return;
                    }
                    if (this->parser_.getContext().state == FrameParser::STATE::PAYLOAD) break;
                }

                if (!header.mask) {
                    co_await writer.close(CLOSE_CODE::PROTOCOL_ERROR, "client frame must be masked (RFC 6455 §5.1)");
                    closeInFlight();
                    co_await this->handler_->onClose(writer);
                    co_await tr->shutdown();
                    co_return;
                }

                const std::uint64_t payload_len = this->parser_.payloadLength();

                if (header.opcode >= OPCODE::CLOSE) {
                    std::string control_payload;
                    control_payload.reserve(static_cast<std::size_t>(payload_len));
                    while (control_payload.size() < payload_len) {
                        if (!co_await fillIfEmpty()) {
                            closeInFlight();
                            co_await this->handler_->onClose(writer);
                            co_await tr->shutdown();
                            co_return;
                        }
                        const std::size_t avail = static_cast<std::size_t>(end - begin);
                        const std::size_t take = std::min<std::size_t>(avail, static_cast<std::size_t>(payload_len) -
                                                                                      control_payload.size());
                        control_payload.append(&*begin, take);
                        begin += take;
                    }
                    if (header.masking_key) { detail::unmaskInPlace(control_payload, *header.masking_key, 0); }
                    this->parser_.advancePayload(payload_len);

                    Frame cf;
                    static_cast<FrameHeader &>(cf) = header;
                    cf.payload = std::move(control_payload);

                    switch (header.opcode) {
                        case OPCODE::PING:
                            usub::uvent::system::co_spawn(this->handler_->ping(writer, cf));
                            break;
                        case OPCODE::PONG:
                            usub::uvent::system::co_spawn(this->handler_->pong(writer, cf));
                            break;
                        case OPCODE::CLOSE:
                            usub::uvent::system::co_spawn(this->handler_->close(writer, cf));
                            closeInFlight();
                            co_await this->handler_->onClose(writer);
                            co_await tr->shutdown();
                            co_return;
                        case OPCODE::RESERVED_B:
                            usub::uvent::system::co_spawn(this->handler_->reservedB(writer, cf));
                            break;
                        case OPCODE::RESERVED_C:
                            usub::uvent::system::co_spawn(this->handler_->reservedC(writer, cf));
                            break;
                        case OPCODE::RESERVED_D:
                            usub::uvent::system::co_spawn(this->handler_->reservedD(writer, cf));
                            break;
                        case OPCODE::RESERVED_E:
                            usub::uvent::system::co_spawn(this->handler_->reservedE(writer, cf));
                            break;
                        case OPCODE::RESERVED_F:
                            usub::uvent::system::co_spawn(this->handler_->reservedF(writer, cf));
                            break;
                        default:
                            break;
                    }

                    this->parser_.reset();
                    continue;
                }

                const bool is_opener = (header.opcode >= OPCODE::TEXT && header.opcode <= OPCODE::RESERVED_7);

                if (is_opener && this->in_flight_) {
                    co_await writer.close(CLOSE_CODE::PROTOCOL_ERROR, "new data frame mid-fragmented-message");
                    closeInFlight();
                    co_await this->handler_->onClose(writer);
                    co_await tr->shutdown();
                    co_return;
                }
                if (header.opcode == OPCODE::CONTINUATION && !this->in_flight_) {
                    co_await writer.close(CLOSE_CODE::PROTOCOL_ERROR,
                                          "continuation frame outside a fragmented message");
                    co_await this->handler_->onClose(writer);
                    co_await tr->shutdown();
                    co_return;
                }

                if (is_opener) {
                    this->in_flight_ = std::make_shared<ClientReaderChannel>();
                    auto reader = std::make_shared<ClientReader>(this->in_flight_);

                    co_await this->in_flight_->pushHeader(header);

                    auto handler = this->handler_;
                    switch (header.opcode) {
                        case OPCODE::TEXT:
                            usub::uvent::system::co_spawn(
                                    [](std::shared_ptr<HandlerT> handler,
                                       std::shared_ptr<ClientReader> reader,
                                       ServerWriter writer) -> usub::uvent::task::Awaitable<void> {
                                        co_await handler->text(*reader, std::move(writer));
                                    }(handler, reader, writer));
                            break;
                        case OPCODE::BINARY:
                            usub::uvent::system::co_spawn(
                                    [](std::shared_ptr<HandlerT> handler,
                                       std::shared_ptr<ClientReader> reader,
                                       ServerWriter writer) -> usub::uvent::task::Awaitable<void> {
                                        co_await handler->binary(*reader, std::move(writer));
                                    }(handler, reader, writer));
                            break;
                        case OPCODE::RESERVED_3:
                            usub::uvent::system::co_spawn(
                                    [](std::shared_ptr<HandlerT> handler,
                                       std::shared_ptr<ClientReader> reader,
                                       ServerWriter writer) -> usub::uvent::task::Awaitable<void> {
                                        co_await handler->reserved3(*reader, std::move(writer));
                                    }(handler, reader, writer));
                            break;
                        case OPCODE::RESERVED_4:
                            usub::uvent::system::co_spawn(
                                    [](std::shared_ptr<HandlerT> handler,
                                       std::shared_ptr<ClientReader> reader,
                                       ServerWriter writer) -> usub::uvent::task::Awaitable<void> {
                                        co_await handler->reserved4(*reader, std::move(writer));
                                    }(handler, reader, writer));
                            break;
                        case OPCODE::RESERVED_5:
                            usub::uvent::system::co_spawn(
                                    [](std::shared_ptr<HandlerT> handler,
                                       std::shared_ptr<ClientReader> reader,
                                       ServerWriter writer) -> usub::uvent::task::Awaitable<void> {
                                        co_await handler->reserved5(*reader, std::move(writer));
                                    }(handler, reader, writer));
                            break;
                        case OPCODE::RESERVED_6:
                            usub::uvent::system::co_spawn(
                                    [](std::shared_ptr<HandlerT> handler,
                                       std::shared_ptr<ClientReader> reader,
                                       ServerWriter writer) -> usub::uvent::task::Awaitable<void> {
                                        co_await handler->reserved6(*reader, std::move(writer));
                                    }(handler, reader, writer));
                            break;
                        case OPCODE::RESERVED_7:
                            usub::uvent::system::co_spawn(
                                    [](std::shared_ptr<HandlerT> handler,
                                       std::shared_ptr<ClientReader> reader,
                                       ServerWriter writer) -> usub::uvent::task::Awaitable<void> {
                                        co_await handler->reserved7(*reader, std::move(writer));
                                    }(handler, reader, writer));
                            break;
                        default:
                            break;
                    }
                } else {
                    co_await this->in_flight_->pushHeader(header);
                }

                std::uint64_t remaining = payload_len;
                while (remaining > 0) {
                    if (!co_await fillIfEmpty()) {
                        closeInFlight();
                        co_await this->handler_->onClose(writer);
                        co_await tr->shutdown();
                        co_return;
                    }
                    const std::size_t avail = static_cast<std::size_t>(end - begin);
                    const std::size_t take = std::min<std::size_t>(avail, static_cast<std::size_t>(remaining));
                    std::string chunk(&*begin, take);
                    begin += take;
                    remaining -= take;
                    this->parser_.advancePayload(take);

                    if (!co_await this->in_flight_->pushPayload(std::move(chunk))) { break; }
                }

                if (header.fin) {
                    this->in_flight_->close();
                    this->in_flight_.reset();
                }
                this->parser_.reset();
            }
        }

        std::shared_ptr<HandlerT> handler_;
        std::shared_ptr<WriteState> write_state_;
        FrameParser parser_{};
        std::shared_ptr<ClientReaderChannel> in_flight_{};
    };

}// namespace usub::unet::ws
