#include "unet/http/v1/wire/response_serializer.hpp"

namespace usub::unet::http::v1 {
    ResponseSerializer::SerializerContext &ResponseSerializer::getContext() { return this->context_; }

    std::string ResponseSerializer::serialize(const Response &response) {
        std::string rv;
        rv.reserve(16 * 1024);
        auto code = static_cast<std::size_t>(response.metadata.status_code);

        rv.append("HTTP/1.1 ");//TODO: propper version
        rv.append(std::to_string(code));
        rv.append(" ");
        std::string msg = "Unknown";
        if (code < status_messages.size() && !status_messages[code].empty()) { msg = status_messages[code]; }
        if (response.metadata.status_message && !response.metadata.status_message->empty()) {
            msg = *response.metadata.status_message;
        }
        rv.append(msg);
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
