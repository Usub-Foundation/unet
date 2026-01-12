#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "unet/http/request.hpp"
#include "unet/http/response.hpp"
#include "unet/http/router/route.hpp"
#include "unet/http/session.hpp"
#include "unet/http/v2/frames.hpp"
#include "unet/http/v2/request_parser.hpp"
#include "unet/http/v2/response_serializer.hpp"


namespace usub::unet::http {

    namespace v2 {

        static std::size_t frame_size;

        struct Stream {
            Request request{};
            Response response{};
            router::Route *route = nullptr;
            RequestParser parser{};
        };

        struct Settings {
            std::uint32_t header_table_size{4096};
            std::uint32_t enable_push{1};
            std::uint32_t max_concurrent_streams{0xffffffffu};
            std::uint32_t initial_window_size{65535};
            std::uint32_t max_frame_size{frame_size};
            std::uint32_t max_header_list_size{max_headers_size};
        };

        static constexpr std::size_t frame_header_size = 9;
        static constexpr std::string_view preface = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    }// namespace v2

    template<typename RouterType>
    class ServerSession<VERSION::HTTP_2_0, RouterType> {
    public:
        enum class STATE : std::uint8_t {
            EXPECT_PREFACE,
            // Initial Settings
            INITIAL_SETTINGS,
            INITIAL_SETTINGS_AWAIT_HEADER,
            INITIAL_SETTINGS_AWAIT_FRAME,
            // Process Data in order
            READY,
            AWAIT_HEADER,
            AWAIT_FRAME,
            // Other
            GOAWAY_SENT,
            DRAINING,
            CLOSED,
            FAILED,
        };

        explicit ServerSession(std::shared_ptr<RouterType> router) : router_(std::move(router)) {}
        ServerSession() = delete;
        ~ServerSession() = default;

        usub::uvent::task::Awaitable<void> on_read(std::string_view data, usub::uvent::net::TCPClientSocket &socket) {
            // TODO: Remove unnecesarry buffering
            this->read_buffer_.append(data.data(), data.size());
            std::string_view::const_iterator begin = data.begin();
            const std::string_view::const_iterator end = data.end();
            STATE &state = this->state_;

            while (begin != end) {
                switch (state) {
                    case STATE::EXPECT_PREFACE: {
                        if (data.size() < v2::preface.size()) {
                            state = STATE::FAILED;
                            // TODO: GoAway, close
                            co_return;
                        }
                        if (data.compare(0, v2::preface.size(), v2::preface) != 0) {
                            state = STATE::FAILED;
                            co_return;
                        }
                        begin += v2::preface.size();
                        state = STATE::EXPECT_SETTINGS;

                        std::string frame = this->build_settings_frame();
                        ssize_t wrsz = co_await socket.async_write(reinterpret_cast<const uint8_t *>(frame.data()),
                                                                   frame.size());
                        if (wrsz <= 0) { state = STATE::FAILED; }
                        [[fallthrough]];
                    }
                    case STATE::INITIAL_SETTINGS: {
                        if (begin == end) { co_return; }
                        if ((end - begin) < frame_header_size) [[unlikely]] {
                            this->read_buffer_.reserve(frame_header_size);
                            this->read_buffer_.insert(read_buffer_.end(), begin, end);
                            state = STATE::SETTINGS_AWAIT_HEADER;
                            co_return;
                        }
                        this->current_header_ = this->parse_frame_header(begin);
                        if (this->current_header_.type != v2::FRAME_TYPE::SETTINGS &&
                            this->current_header_.stream_identifier != 0) [[unlikely]] {
                            state = STATE::FAILED;
                        }
                        // TODO: Check if its ACK for settings we sent before
                        if ((end - begin) < this->current_header_.length) [[unlikely]] {
                            this->read_buffer_.reserve(this->current_header_.length);
                            this->read_buffer_.insert(read_buffer_.end(), begin, end);
                            state = STATE::SETTINGS_NEED_MORE;
                            begin = end;
                            co_return;
                        }
                        v2::SettingsPayload payload = this->parseSettingsPayload(begin, this->current_header_.length);
                        for (auto setting: payload.settings) {
                            switch (setting.identifier) {
                                case v2::SETTINGS::SETTINGS_HEADER_TABLE_SIZE:
                                    this->client_settings_.header_table_size = setting.value;
                                    break;
                                case v2::SETTINGS::SETTINGS_ENABLE_PUSH:
                                    this->client_settings_.enable_push = setting.value;
                                    break;
                                case v2::SETTINGS::SETTINGS_MAX_CONCURRENT_STREAMS:
                                    this->client_settings_.max_concurrent_streams setting.value;
                                    break;
                                case v2::SETTINGS::SETTINGS_INITIAL_WINDOW_SIZE:
                                    this->client_settings_.initial_window_size = setting.value;
                                    break;
                                case v2::SETTINGS::SETTINGS_MAX_FRAME_SIZE:
                                    this->client_settings_.max_frame_size = setting.value;
                                    break;
                                case v2::SETTINGS::SETTINGS_MAX_HEADER_LIST_SIZE:
                                    this->client_settings_.max_header_list_size = setting.value;
                                    break;
                                default:
                                    // Ignore unknown
                                    break;
                            }
                        }
                        this->current_header_ = {};
                        state = STATE::READY;
                        [[fallthrough]];
                    }
                    case STATE::READY: {
                        if (begin == end) { co_return; }
                    }
                    case STATE::FAILED:
                        [[fallthrough]];
                    case STATE::CLOSED: {
                        co_return;
                    }
                    case STATE::INITIAL_SETTINGS_AWAIT_HEADER: {
                    }
                    case STATE::INITIAL_SETTINGS_AWAIT_FRAME: {
                    }
                    default:
                        break;
                }
                co_return;
            }
        }

        usub::uvent::task::Awaitable<void> on_close() {
            this->streams_.clear();
            co_return;
        }

        usub::uvent::task::Awaitable<void> on_error(int) {
            this->streams_.clear();
            co_return;
        }

    private:
        static inline std::uint32_t read_u24(const std::string_view::const_iterator &begin) {
            return (static_cast<std::uint32_t>(static_cast<std::uint8_t>(begin[0])) << 16) |
                   (static_cast<std::uint32_t>(static_cast<std::uint8_t>(begin[1])) << 8) |
                   static_cast<std::uint32_t>(static_cast<std::uint8_t>(begin[2]));
        }

        static inline std::uint32_t read_u32(const std::string_view::const_iterator &begin) {
            return (static_cast<std::uint32_t>(static_cast<std::uint8_t>(begin[0])) << 24) |
                   (static_cast<std::uint32_t>(static_cast<std::uint8_t>(begin[1])) << 16) |
                   (static_cast<std::uint32_t>(static_cast<std::uint8_t>(begin[2])) << 8) |
                   static_cast<std::uint32_t>(static_cast<std::uint8_t>(begin[3]));
        }

        static inline v2::FrameHeader parseFrameHeader(std::string_view::const_iterator &begin) {
            v2::FrameHeader header{};
            header.length = read_u24(begin);
            begin += 3;
            header.type = static_cast<FRAME_TYPE>(static_cast<std::uint8_t>(*begin));
            ++begin;
            header.flags = static_cast<std::uint8_t>(*begin);
            ++begin;
            header.stream_identifier = read_u32(begin) & 0x7fffffff;
            begin += 4;
            return header;
        }

        static inline v2::SettingsPayload parseSettingsPayload(std::string_view::const_iterator &begin,
                                                               std::size_t length) {
            v2::SettingsPayload payload;
            payload.settings.reserve(length / 6);
            for (std::size_t offset = 0; offset < length; offset += 6) {
                auto id = static_cast<std::uint16_t>((static_cast<std::uint8_t>(begin[0]) << 8) |
                                                     static_cast<std::uint8_t>(begin[1]));
                std::uint32_t value = (static_cast<std::uint32_t>(static_cast<std::uint8_t>(begin[2])) << 24) |
                                      (static_cast<std::uint32_t>(static_cast<std::uint8_t>(begin[3])) << 16) |
                                      (static_cast<std::uint32_t>(static_cast<std::uint8_t>(begin[4])) << 8) |
                                      static_cast<std::uint32_t>(static_cast<std::uint8_t>(begin[5]));
                payload.settings.push_back(v2::Setting{static_cast<v2::SETTINGS>(id), value});
                begin += 6;
            }

            return payload;
        }

        static void append_frame_header(std::string &out, std::uint32_t length, FRAME_TYPE type, std::uint8_t flags,
                                        std::uint32_t stream_id) {
            stream_id &= 0x7fffffff;
            char header[v2::frame_header_size]{
                    static_cast<char>((length >> 16) & 0xff),
                    static_cast<char>((length >> 8) & 0xff),
                    static_cast<char>(length & 0xff),
                    static_cast<char>(type),
                    static_cast<char>(flags),
                    static_cast<char>((stream_id >> 24) & 0x7f),
                    static_cast<char>((stream_id >> 16) & 0xff),
                    static_cast<char>((stream_id >> 8) & 0xff),
                    static_cast<char>(stream_id & 0xff),
            };
            out.append(header, v2::frame_header_size);
        }

        static void append_frame(std::string &out, FRAME_TYPE type, std::uint8_t flags, std::uint32_t stream_id,
                                 std::string_view payload) {
            append_frame_header(out, static_cast<std::uint32_t>(payload.size()), type, flags, stream_id);
            out.append(payload.data(), payload.size());
        }

        static void append_setting(std::string &out, std::uint16_t identifier, std::uint32_t value) {
            out.push_back(static_cast<char>((identifier >> 8) & 0xff));
            out.push_back(static_cast<char>(identifier & 0xff));
            out.push_back(static_cast<char>((value >> 24) & 0xff));
            out.push_back(static_cast<char>((value >> 16) & 0xff));
            out.push_back(static_cast<char>((value >> 8) & 0xff));
            out.push_back(static_cast<char>(value & 0xff));
        }

        std::string build_settings_frame() const {
            std::string payload;
            payload.reserve(6 * 6);
            append_setting(payload, 0x01, this->settings_.local.header_table_size);
            append_setting(payload, 0x02, this->settings_.local.enable_push);
            if (this->settings_.local.max_concurrent_streams != 0) {
                append_setting(payload, 0x03, this->settings_.local.max_concurrent_streams);
            }
            append_setting(payload, 0x04, this->settings_.local.initial_window_size);
            append_setting(payload, 0x05, this->settings_.local.max_frame_size);
            if (this->settings_.local.max_header_list_size != 0) {
                append_setting(payload, 0x06, this->settings_.local.max_header_list_size);
            }

            std::string frame;
            frame.reserve(v2::frame_header_size + payload.size());
            append_frame(frame, FRAME_TYPE::SETTINGS, 0, 0, payload);
            return frame;
        }

        bool ensure_route(v2::Stream &stream) {
            if (stream.route) { return true; }
            stream.request.metadata.version = VERSION::HTTP_2_0;
            stream.response.metadata.version = VERSION::HTTP_2_0;
            auto match = this->router_->match(stream.request);
            if (!match) {
                stream.response.metadata.status_code = match.error();
                return false;
            }
            stream.route = match.value();
            return true;
        }

        bool invoke_middleware(const MIDDLEWARE_PHASE &phase, v2::Stream &stream) {
            return this->router_->getMiddlewareChain().execute(phase, stream.request, stream.response)
                           ? (stream.route
                                      ? stream.route->middleware_chain.execute(phase, stream.request, stream.response)
                                      : true)
                           : false;
        }

        void append_header_block(std::string &out, std::uint32_t stream_id, std::string_view block, bool end_stream) {
            if (block.size() <= this->settings_.peer.max_frame_size) {
                std::uint8_t flags = FLAGS::END_HEADERS | (end_stream ? FLAGS::END_STREAM : 0);
                append_frame(out, FRAME_TYPE::HEADERS, flags, stream_id, block);
                return;
            }

            std::size_t offset = 0;
            std::size_t chunk = this->settings_.peer.max_frame_size;
            std::uint8_t first_flags = end_stream ? FLAGS::END_STREAM : 0;
            append_frame(out, FRAME_TYPE::HEADERS, first_flags, stream_id, block.substr(offset, chunk));
            offset += chunk;
            while (offset < block.size()) {
                chunk = std::min<std::size_t>(this->settings_.peer.max_frame_size, block.size() - offset);
                std::uint8_t flags = (offset + chunk >= block.size()) ? FLAGS::END_HEADERS : 0;
                append_frame(out, FRAME_TYPE::CONTINUATION, flags, stream_id, block.substr(offset, chunk));
                offset += chunk;
            }
        }

        void append_data_frames(std::string &out, std::uint32_t stream_id, std::string_view data) {
            if (data.empty()) {
                append_frame(out, FRAME_TYPE::DATA, FLAGS::END_STREAM, stream_id, {});
                return;
            }
            std::size_t offset = 0;
            while (offset < data.size()) {
                std::size_t chunk = std::min<std::size_t>(this->settings_.peer.max_frame_size, data.size() - offset);
                std::uint8_t flags = (offset + chunk >= data.size()) ? FLAGS::END_STREAM : 0;
                append_frame(out, FRAME_TYPE::DATA, flags, stream_id, data.substr(offset, chunk));
                offset += chunk;
            }
        }

        usub::uvent::task::Awaitable<void> write_response(std::uint32_t stream_id, v2::Stream &stream,
                                                          usub::uvent::net::TCPClientSocket &socket) {
            if (stream.response_started) { co_return; }
            stream.response_started = true;

            if (stream.response.metadata.status_code == 0) { stream.response.metadata.status_code = 200; }
            stream.response.metadata.version = VERSION::HTTP_2_0;

            std::string header_block = v2::ResponseSerializer::serialize_headers(stream.response, this->encoder_);
            const bool end_stream = stream.response.body.empty();

            std::string out;
            out.reserve(v2::frame_header_size * 2 + header_block.size() + stream.response.body.size());
            this->append_header_block(out, stream_id, header_block, end_stream);
            if (!end_stream) { this->append_data_frames(out, stream_id, stream.response.body); }

            ssize_t wrsz = co_await socket.async_write(reinterpret_cast<const uint8_t *>(out.data()), out.size());
            if (wrsz <= 0) { this->state_ = STATE::FAILED; }
            co_return;
        }

        usub::uvent::task::Awaitable<void> handle_stream_complete(std::uint32_t stream_id, v2::Stream &stream,
                                                                  usub::uvent::net::TCPClientSocket &socket) {
            if (!this->ensure_route(stream)) {
                this->router_->error("log", stream.request, stream.response);
                this->router_->error(std::to_string(stream.response.metadata.status_code), stream.request,
                                     stream.response);
                co_await this->write_response(stream_id, stream, socket);
                co_return;
            }

            auto handler = stream.route->handler;
            co_await handler(stream.request, stream.response);
            co_await this->write_response(stream_id, stream, socket);
            co_return;
        }

        std::unordered_map<std::uint32_t, v2::Stream> streams_{};
        std::shared_ptr<RouterType> router_;
        v2::ResponseSerializer response_writer_{};
        v2::hpack::Decoder decoder_{};
        v2::hpack::Encoder encoder_{};

        v2::Settings client_settings_;// Client Settings
        v2::Settings server_settings_;// Our settings

        std::queue<std::pair<SettingsPayload, std::chrono::steady_clock::time_point>>
                local_unacked_settings;// Settings client hasn't acked yet (settings not in effect yet)

        v2::FrameHeader current_header_{};
        std::vector<std::uint8_t> read_buffer_{};

        STATE state_{STATE::EXPECT_PREFACE};
    };
}// namespace usub::unet::http
