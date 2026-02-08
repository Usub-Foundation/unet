#pragma once

#include <cstdint>

#include <uvent/Uvent.h>

#include "unet/http/v2/frames.hpp"
#include "unet/http/v2/stream.hpp"
#include "unet/http/v2/stream_manager.hpp"

namespace usub::unet::http::v2 {

    class Connection {
    public:
        Connection() = default;
        ~Connection() = default;

        enum class STATE : std::uint8_t {
            PREFACE,
            SETTINGS_EXCHANGE,
            OPEN,
            CLOSING,
            CLOSED,
            FAILED,
        };

        struct ParserContext {
            STATE state{STATE::PREFACE};
            std::uint8_t parser_pos{0};
            FrameHeader current_header{};
        };

        void on_read(std::string_view data, usub::unet::core::stream::Transport &transport) {
            ParserContext &context = this->getContext();
            STATE &state = context.state;
            std::string_view::const_iterator begin = data.begin();
            const std::string_view::const_iterator end = data.end();

            while (begin != end) {
                switch (state) {
                    case STATE::PREFACE: {
                        while (begin != end) {
                            if (*begin != preface_[context.parser_pos++]) {
                                state = STATE::FAILED;
                                // TODO: GOAWAY + PROTOCOL_ERROR
                                return;
                            }
                            ++begin;
                            if (context.parser_pos == this->preface_.size()) {
                                context.parser_pos = 0;
                                data.remove_prefix(this->preface_.size());
                                state = STATE::SETTINGS_EXCHANGE;
                                break;
                            }
                        }
                        break;
                    }
                    case STATE::SETTINGS_EXCHANGE: {
                        auto settings_frame = this->stream_manager_.sendSettings();
                        auto response = this->serialize_frame_to_string(settings_frame);
                        // co_await this->write_response(transport, response);
                        if (!parseFrameHeader(begin, end)) { return; }
                        data.remove_prefix(this->frame_header_size);
                        if (context.current_header.type != FRAME_TYPE::SETTINGS) {
                            state = STATE::FAILED;
                            return;
                        }
                        auto res = this->stream_manager_.handleInitialSettings(context.current_header, data);
                        if (!res) {}
                        data.remove_prefix(context.current_header.length);
                        // co_await this->write_response(transport, res.value());
                        context.current_header = {};
                        context.parser_pos = 0;
                        state = STATE::OPEN;
                    }
                    case STATE::OPEN: {
                        if (!parseFrameHeader(begin, end)) { return; }
                        data.remove_prefix(this->frame_header_size);
                    }
                    default: {
                        exit(11);
                    }
                }
            }
            return;
        }

        bool parseFrameHeader(std::string_view::const_iterator &begin, const std::string_view::const_iterator &end) {
            ParserContext &context = this->getContext();

            while (begin != end) {
                switch (context.parser_pos++) {
                    case 0:
                        context.current_header.length = static_cast<std::uint32_t>(static_cast<std::uint8_t>(*begin++))
                                                        << 16;
                        break;
                    case 1:
                        context.current_header.length |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(*begin++))
                                                         << 8;
                        break;
                    case 2:
                        context.current_header.length |=
                                static_cast<std::uint32_t>(static_cast<std::uint8_t>(*begin++));
                        break;
                    case 3:
                        context.current_header.type = static_cast<FRAME_TYPE>(
                                static_cast<std::uint32_t>(static_cast<std::uint8_t>(*begin++)));
                        break;
                    case 4:
                        context.current_header.flags = static_cast<std::uint32_t>(static_cast<std::uint8_t>(*begin++));
                        break;
                    case 5:
                        context.current_header.stream_identifier =
                                static_cast<std::uint32_t>(static_cast<std::uint8_t>(*begin++)) << 24;
                        break;
                    case 6:
                        context.current_header.stream_identifier |=
                                static_cast<std::uint32_t>(static_cast<std::uint8_t>(*begin++)) << 16;
                        break;
                    case 7:
                        context.current_header.stream_identifier |=
                                static_cast<std::uint32_t>(static_cast<std::uint8_t>(*begin++)) << 8;
                        break;
                    case 8:
                        context.current_header.stream_identifier |=
                                static_cast<std::uint32_t>(static_cast<std::uint8_t>(*begin++));
                        context.current_header.stream_identifier &= 0x7fffffff;// clear reserved bit
                        return true;
                    default:
                        return false;
                }
            }
            return false;// Need more
        }

        inline void write_frame_header_9(std::uint8_t out[9], const FrameHeader &h) {
            // length: 24-bit big-endian
            if (h.length > 0x00FFFFFFu) { throw std::runtime_error("HTTP/2 frame length exceeds 24-bit limit"); }

            out[0] = static_cast<std::uint8_t>((h.length >> 16) & 0xFF);
            out[1] = static_cast<std::uint8_t>((h.length >> 8) & 0xFF);
            out[2] = static_cast<std::uint8_t>(h.length & 0xFF);
            out[3] = static_cast<std::uint8_t>(h.type);
            out[4] = h.flags;

            // stream id: 31 bits, top bit reserved and MUST be 0 on the wire
            std::uint32_t sid = (h.stream_identifier & 0x7FFFFFFFu);
            out[5] = static_cast<std::uint8_t>((sid >> 24) & 0xFF);
            out[6] = static_cast<std::uint8_t>((sid >> 16) & 0xFF);
            out[7] = static_cast<std::uint8_t>((sid >> 8) & 0xFF);
            out[8] = static_cast<std::uint8_t>(sid & 0xFF);
        }

        inline std::string serialize_frame_to_string(const GenericFrame &f) {
            if (f.frame_header.length != static_cast<std::uint32_t>(f.payload.size())) {
                throw std::runtime_error("FrameHeader.length != payload.size()");
            }

            std::string out;
            out.resize(9 + f.payload.size());

            std::uint8_t hdr[9];
            write_frame_header_9(hdr, f.frame_header);

            std::memcpy(out.data(), hdr, 9);

            if (!f.payload.empty()) { std::memcpy(out.data() + 9, f.payload.data(), f.payload.size()); }
            return out;
        }

        ParserContext &getContext() { return this->context_; }

    private:
        ParserContext context_{};
        StreamManager stream_manager_{};


        static inline constexpr std::size_t frame_header_size = 9;
        static inline constexpr std::string_view preface_ = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    };
}// namespace usub::unet::http::v2