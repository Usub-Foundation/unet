#include "unet/http/v1/wire/response_serializer.hpp"

namespace usub::unet::http::v1 {
    ResponseSerializer::SerializerContext &ResponseSerializer::getContext() { return this->context_; }

    std::string ResponseSerializer::serialize(const Response &response) {
        auto code = static_cast<std::size_t>(response.metadata.status_code);
        const std::string status_code = std::to_string(code);
        const std::string content_length = std::to_string(response.body.size());
        std::string_view msg = "Unknown";
        if (code < status_messages.size() && !status_messages[code].empty()) { msg = status_messages[code]; }
        if (response.metadata.status_message && !response.metadata.status_message->empty()) {
            msg = *response.metadata.status_message;
        }

        std::size_t response_size = 9 + status_code.size() + 1 + msg.size() + 2;// "HTTP/1.1 " + code + " " + msg + "\r\n"
        for (const auto &h: response.headers.all()) {
            if (h.key == "content-length") { continue; }
            response_size += h.key.size() + 2 + h.value.size() + 2;
        }
        response_size += 16 + content_length.size() + 4;// "content-length: " + value + "\r\n\r\n"
        response_size += response.body.size();

        std::string rv;
        rv.reserve(response_size);

        rv.append("HTTP/1.1 ");//TODO: propper version
        rv.append(status_code);
        rv.append(" ");
        rv.append(msg);
        rv.append("\r\n");
        for (const auto &h: response.headers.all()) {
            if (h.key == "content-length") { continue; }
            rv.append(h.key);
            rv.append(": ");
            rv.append(h.value);
            rv.append("\r\n");
        }
        rv.append("content-length: ");
        rv.append(content_length);
        rv.append("\r\n");
        rv.append("\r\n");
        rv.append(response.body.data(), response.body.size());
        return rv;
    }
}// namespace usub::unet::http::v1
