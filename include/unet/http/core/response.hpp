#pragma once

#include <optional>
#include <string>

#include "unet/header/generic.hpp"
#include "unet/http/core/message.hpp"

namespace usub::unet::http {

    struct ResponseMetadata {
        VERSION version{};
        std::uint16_t status_code{};
        std::optional<std::string> status_message{};// Allow overwriting the status message for HTTP < 2
    };

    struct Response {
        ResponseMetadata metadata{};
        usub::unet::header::Headers headers{};

        // TODO: std::variant<std::monostate, FileBody(int fd, std::size_t length, std::size_t offset), chunkedGenerator> ??
        std::string body{};

        // Backwards Compatability functions
        Response &setStatus(std::uint16_t status_code) {
            this->metadata.status_code = status_code;
            return *this;
        }
        Response &setStatus(STATUS_CODE status_code) {
            this->metadata.status_code = static_cast<std::uint16_t>(status_code);
            return *this;
        }

        [[deprecated("This function can throw")]] Response &setStatus(std::string status_code) {
            this->metadata.status_code = std::stoi(status_code);
            return *this;
        }


        Response &setBody(const std::string &data, const std::string &content_type = "") {
            this->body = data;
            this->headers.addHeader("Content-Length", std::to_string(data.size()));
            return *this;
        }
        Response &addHeader(const std::string &key, const std::string &value) {
            std::string k = key;
            std::string v = value;
            this->headers.addHeader(std::move(k), std::move(v));
            return *this;
        }
    };

}// namespace usub::unet::http