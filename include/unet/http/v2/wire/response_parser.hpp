#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include "unet/http/core/response.hpp"
#include "unet/http/v2/wire/hpack.hpp"
#include "unet/http/v2/wire/types.hpp"


namespace usub::unet::http::v2 {

    class ResponseParser {
    public:
        enum class STATE : std::uint8_t {
            HEADERS,
            HEADERS_DONE,
            BODY,
            TRAILERS,
            DONE,
            FAILED,
        };

        STATE state() const noexcept { return state_; }
        bool  isDone() const noexcept { return state_ == STATE::DONE; }

        void appendHeaderFragment(std::span<const std::byte> fragment) {
            header_acc_.insert(header_acc_.end(), fragment.begin(), fragment.end());
        }

        std::expected<void, ERROR_CODE>
        decodeHeaders(HpackDecoder &decoder, usub::unet::http::ResponseWriter &out);

        void appendBodyChunk(std::span<const std::byte> data) {
            if (state_ == STATE::HEADERS_DONE) state_ = STATE::BODY;
            data_acc_.insert(data_acc_.end(), data.begin(), data.end());
        }

        void beginTrailers() noexcept {
            if (state_ == STATE::HEADERS_DONE || state_ == STATE::BODY) {
                state_ = STATE::TRAILERS;
                header_acc_.clear();
            }
        }

        std::expected<void, ERROR_CODE>
        decodeTrailers(HpackDecoder &decoder, usub::unet::http::ResponseWriter &out);

        void markEndStream() noexcept {
            end_stream_pending_ = true;
            if (state_ == STATE::HEADERS_DONE || state_ == STATE::BODY ||
                state_ == STATE::TRAILERS)
                state_ = STATE::DONE;
        }

        std::string takePendingBody() noexcept {
            std::string out;
            if (data_acc_.empty()) return out;
            out.assign(reinterpret_cast<const char *>(data_acc_.data()), data_acc_.size());
            data_acc_.clear();
            return out;
        }

    private:
        STATE                  state_{STATE::HEADERS};
        bool                   end_stream_pending_{false};
        std::vector<std::byte> header_acc_{};
        std::vector<std::byte> data_acc_{};
    };

}// namespace usub::unet::http::v2
