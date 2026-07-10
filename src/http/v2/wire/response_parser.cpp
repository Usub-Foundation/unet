#include "unet/http/v2/wire/response_parser.hpp"

#include <string>

#include "unet/http/core/message.hpp"


namespace usub::unet::http::v2 {

    namespace {
        bool isLowercaseFieldName(std::string_view name) noexcept {
            for (char c : name) {
                if (c >= 'A' && c <= 'Z') return false;
            }
            return true;
        }

        bool isForbiddenH2Header(std::string_view name) noexcept {
            return name == "connection" || name == "keep-alive" ||
                   name == "proxy-connection" || name == "transfer-encoding" ||
                   name == "upgrade";
        }
    }// namespace


    std::expected<void, ERROR_CODE>
    ResponseParser::decodeHeaders(HpackDecoder &decoder,
                                    usub::unet::http::ResponseWriter &out) {
        std::string_view block{reinterpret_cast<const char *>(header_acc_.data()),
                                 header_acc_.size()};
        auto decoded = decoder.decode(block);
        header_acc_.clear();
        header_acc_.shrink_to_fit();
        if (!decoded) return std::unexpected(ERROR_CODE::COMPRESSION_ERROR);

        bool seen_status = false;
        bool in_pseudo   = true;

        for (auto &field : *decoded) {
            const std::string &name = field.name;
            if (name.empty()) return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
            if (!isLowercaseFieldName(name)) return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);

            if (name[0] == ':') {
                if (!in_pseudo) return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
                if (name == ":status") {
                    if (seen_status) return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
                    seen_status = true;
                    std::uint32_t code = 0;
                    for (char c : field.value) {
                        if (c < '0' || c > '9') return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
                        code = code * 10 + static_cast<std::uint32_t>(c - '0');
                        if (code > 999) return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
                    }
                    out.metadata.status_code = static_cast<std::uint16_t>(code);
                } else {
                    return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
                }
                continue;
            }

            in_pseudo = false;

            if (isForbiddenH2Header(name)) {
                return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
            }

            out.headers.addHeader(name, field.value);
        }

        if (!seen_status) return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);

        out.metadata.version = VERSION::HTTP_2_0;
        state_ = end_stream_pending_ ? STATE::DONE : STATE::HEADERS_DONE;
        return {};
    }


    std::expected<void, ERROR_CODE>
    ResponseParser::decodeTrailers(HpackDecoder &decoder,
                                     usub::unet::http::ResponseWriter &out) {
        std::string_view block{reinterpret_cast<const char *>(header_acc_.data()),
                                 header_acc_.size()};
        auto decoded = decoder.decode(block);
        header_acc_.clear();
        header_acc_.shrink_to_fit();
        if (!decoded) return std::unexpected(ERROR_CODE::COMPRESSION_ERROR);

        for (auto &field : *decoded) {
            const std::string &name = field.name;
            if (name.empty()) return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
            if (!isLowercaseFieldName(name)) return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
            // RFC 9113 §8.1: trailers MUST NOT include pseudo-headers.
            if (name[0] == ':') return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
            if (isForbiddenH2Header(name)) return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
            out.trailers.addHeader(name, field.value);
        }

        // Trailers always carry END_STREAM per RFC 9113 §8.1.
        state_ = STATE::DONE;
        return {};
    }

}// namespace usub::unet::http::v2
