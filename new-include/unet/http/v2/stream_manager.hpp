#pragma once

#include "unet/http/v2/error.hpp"
#include "unet/http/v2/frames.hpp"
#include "unet/http/v2/hpack.hpp"
#include "unet/http/v2/stream.hpp"

namespace usub::unet::http::v2 {
    class StreamManager {
    public:
        bool contains(const std::uint32_t stream_id);
        Stream &at(const std::uint32_t stream_id);
        const Stream &at(const std::uint32_t stream_id) const;

        std::expected<std::string /*ack_frame*/, ERROR_CODE> handleInitialSettings(FrameHeader &frame_header,
                                                                                   std::string_view data);

        std::expected<std::string /*ack_frame*/, ERROR_CODE> handleFrame(FrameHeader &frame_header,
                                                                         std::string_view data);

        v2::GenericFrame sendSettings();


    private:
        std::unordered_map<std::uint32_t, Stream> streams_{};
        ControlStream control_stream_{};
        std::set<std::uint32_t> closed_streams_{};

        std::vector<std::byte> unfinished_header_block_fragment_{};

        hpack::Decoder hpack_decoder_{};
        hpack::Encoder hpack_encoder_{};

        std::uint32_t highest_local_stream_id_ = 0;
        std::uint32_t highest_remote_stream_id_ = 0;
    };
}// namespace usub::unet::http::v2