#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "unet/http/core/response.hpp"
#include "unet/http/v2/wire/hpack.hpp"


namespace usub::unet::http::v2 {

    class ResponseSerializer {
    public:
        std::string serialize(std::uint32_t stream_id,
                               const usub::unet::http::ResponseWriter &res,
                               HpackEncoder &encoder,
                               std::uint32_t max_frame_size,
                               bool          end_stream = true);
    };

}// namespace usub::unet::http::v2
