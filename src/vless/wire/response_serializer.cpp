#include "unet/vless/wire/response_serializer.hpp"

#include <cstdint>

namespace usub::unet::vless {

    std::string ResponseSerializer::serialize(const Response &response) {
        const auto addons_len = static_cast<std::uint8_t>(response.addons.size());

        std::string out;
        out.reserve(2u + addons_len);

        out.push_back(static_cast<char>(response.protocol_version));
        out.push_back(static_cast<char>(addons_len));
        for (const auto b: response.addons) out.push_back(static_cast<char>(b));

        return out;
    }

}// namespace usub::unet::vless
