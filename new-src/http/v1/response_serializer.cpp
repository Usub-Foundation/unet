#include "unet/http/v1/response_serializer.hpp"

namespace usub::unet::http::v1 {
    std::string ResponseSerializer::serialize(const Response &response) {
        std::string rv;
        rv.reserve(16 * 1024);
        rv.append("HTTP/1.1 ");//TODO: propper version
        rv.append(std::to_string(response.metadata.status_code));
        rv.append(" ");
        rv.append(response.metadata.status_message.has_value() ? response.metadata.status_message.value() : status_messages[response.metadata.status_code]);
        rv.append("\r\n");
        for (const auto &h: response.headers.all()) {
            rv.append(h.key);
            rv.append(": ");
            rv.append(h.value);
            rv.append("\r\n");
        }
        rv.append("\r\n");
        rv.append(response.body);
        return rv;
    }
}// namespace usub::unet::http::v1