#include "unet/http/v2/wire/request_parser.hpp"

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
    }// namespace


    std::expected<bool, ERROR_CODE>
    RequestParser::decodeHeaders(HpackDecoder &decoder,
                                   usub::unet::http::RequestReader &out) {
        std::string_view block{reinterpret_cast<const char *>(context_.header_acc.data()),
                                 context_.header_acc.size()};
        auto decoded = decoder.decode(block);
        context_.header_acc.clear();
        context_.header_acc.shrink_to_fit();
        if (!decoded) return std::unexpected(ERROR_CODE::COMPRESSION_ERROR);

        bool tunnel{false};
        bool seen_method{false}, seen_scheme{false}, seen_path{false}, seen_authority{false};
        bool seen_protocol{false};
        bool in_pseudo{true};

        for (auto &field : *decoded) {
            const std::string &name = field.name;
            if (name.empty()) return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
            if (!isLowercaseFieldName(name)) return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);

            if (name[0] == ':') {
                if (!in_pseudo) return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
                if (name == ":method") {
                    if (seen_method) return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
                    seen_method = true;
                    out.metadata.method_token = field.value;
                    if (field.value == "CONNECT") tunnel = true;
                } else if (name == ":scheme") {
                    if (seen_scheme) return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
                    seen_scheme = true;
                    out.metadata.uri.scheme = field.value;
                } else if (name == ":authority") {
                    if (seen_authority) return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
                    seen_authority = true;
                    out.metadata.authority = field.value;
                    const auto colon = field.value.find_last_of(':');
                    if (colon != std::string::npos && field.value.find(']') == std::string::npos) {
                        out.metadata.uri.authority.host = field.value.substr(0, colon);
                        std::uint32_t port = 0;
                        for (char c : field.value.substr(colon + 1)) {
                            if (c < '0' || c > '9') { port = 0; break; }
                            port = port * 10 + static_cast<std::uint32_t>(c - '0');
                            if (port > 65535) { port = 0; break; }
                        }
                        out.metadata.uri.authority.port = static_cast<std::uint16_t>(port);
                    } else {
                        out.metadata.uri.authority.host = field.value;
                    }
                } else if (name == ":path") {
                    if (seen_path) return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
                    seen_path = true;
                    std::string &p = field.value;
                    auto qpos = p.find('?');
                    if (qpos == std::string::npos) {
                        out.metadata.uri.path = std::move(p);
                    } else {
                        out.metadata.uri.path  = p.substr(0, qpos);
                        out.metadata.uri.query = p.substr(qpos + 1);
                    }
                } else if (name == ":protocol") {
                    // RFC 8441 §4 — Extended CONNECT.
                    if (seen_protocol) return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
                    seen_protocol = true;
                    out.headers.addHeader(name, field.value);
                } else {
                    return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
                }
                continue;
            }

            in_pseudo = false;

            // Forbidden in HTTP/2 per RFC 9113 §8.2.2.
            if (name == "connection" || name == "keep-alive" || name == "proxy-connection" ||
                name == "transfer-encoding" || name == "upgrade") {
                return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
            }
            if (name == "te" && field.value != "trailers") {
                return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
            }

            out.headers.addHeader(name, field.value);
        }

        if (tunnel) {
            if (seen_protocol) {
                // RFC 8441 Extended CONNECT.
                if (!seen_scheme || !seen_path || !seen_authority)
                    return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
            } else {
                // RFC 9113 §8.5 plain CONNECT.
                if (seen_scheme || seen_path || !seen_authority)
                    return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
            }
        } else {
            if (!seen_method || !seen_scheme || !seen_path)
                return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
            // RFC 9113 §8.3.1.
            if (out.metadata.uri.path.empty()) return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
            if (seen_protocol)
                return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
        }

        // RFC 9113 §8.1.2.6.
        if (auto cl = out.headers.value("content-length")) {
            std::uint64_t value = 0;
            if (cl->empty()) return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
            for (char c: *cl) {
                if (c < '0' || c > '9') return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
                value = value * 10 + static_cast<std::uint64_t>(c - '0');
            }
            context_.expected_body_bytes = value;
        }

        out.metadata.version = VERSION::HTTP_2_0;
        context_.state = context_.end_stream_pending ? STATE::DONE : STATE::HEADERS_DONE;
        return tunnel;
    }

    std::expected<void, ERROR_CODE>
    RequestParser::decodeTrailers(HpackDecoder &decoder, usub::unet::http::RequestReader &out) {
        std::string_view block{reinterpret_cast<const char *>(context_.header_acc.data()),
                                 context_.header_acc.size()};
        auto decoded = decoder.decode(block);
        context_.header_acc.clear();
        context_.header_acc.shrink_to_fit();
        if (!decoded) return std::unexpected(ERROR_CODE::COMPRESSION_ERROR);

        for (auto &field: *decoded) {
            const std::string &name = field.name;
            if (name.empty()) return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
            if (!isLowercaseFieldName(name)) return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
            // RFC 9113 §8.1 — trailers MUST NOT contain pseudo-header fields.
            if (name[0] == ':') return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
            if (name == "connection" || name == "keep-alive" || name == "proxy-connection" ||
                name == "transfer-encoding" || name == "upgrade") {
                return std::unexpected(ERROR_CODE::PROTOCOL_ERROR);
            }
            out.trailers.addHeader(name, field.value);
        }

        context_.state = STATE::DONE;
        return {};
    }

}// namespace usub::unet::http::v2
