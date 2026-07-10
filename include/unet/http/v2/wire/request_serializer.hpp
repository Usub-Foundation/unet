#pragma once

#include <cstdint>
#include <string>

#include "unet/http/core/request.hpp"
#include "unet/http/v2/wire/hpack.hpp"


namespace usub::unet::http::v2 {

    class RequestSerializer {
    public:
        // Emits HEADERS (+ CONTINUATION). Body flows separately as DATA.
        std::string serialize(std::uint32_t stream_id,
                               const usub::unet::http::RequestReader &req,
                               HpackEncoder &encoder,
                               std::uint32_t max_frame_size,
                               bool          end_stream = true);
    };

}// namespace usub::unet::http::v2
