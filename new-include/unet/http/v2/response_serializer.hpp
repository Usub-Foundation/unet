#pragma once

#include <string>
#include <vector>

#include "unet/http/response.hpp"
#include "unet/http/v2/hpack.hpp"

namespace usub::unet::http::v2 {

    class ResponseSerializer {
    public:
        ResponseSerializer() = default;
        ~ResponseSerializer() = default;

        static std::string serialize_headers(Response &response, hpack::Encoder &encoder, bool use_huffman = true) {
            std::vector<hpack::HeaderField> fields;
            fields.reserve(response.headers.size() + 1);
            fields.push_back(hpack::HeaderField{":status", std::to_string(response.metadata.status_code),
                                                hpack::Indexing::Incremental});
            for (const auto &header: response.headers.all()) {
                fields.push_back(hpack::HeaderField{header.key, header.value, hpack::Indexing::Incremental});
            }
            return encoder.encode(fields, use_huffman);
        }
    };
}// namespace usub::unet::http::v2
