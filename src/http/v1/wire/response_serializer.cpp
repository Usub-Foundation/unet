#include "unet/http/v1/wire/response_serializer.hpp"

#include <cstdio>

#include "unet/http/core/message.hpp"

namespace usub::unet::http::v1 {

    namespace {
        std::string_view phraseFor(const ResponseMetadata &metadata) noexcept {
            if (metadata.status_message && !metadata.status_message->empty()) {
                return *metadata.status_message;
            }
            const std::size_t code = metadata.status_code;
            if (code < status_messages.size() && !status_messages[code].empty()) {
                return status_messages[code];
            }
            return std::string_view{"Unknown"};
        }

        // status line + user headers (content-length stripped) + Content-Length:N
        // + the blank line. Body, if any, is appended by the caller.
        void appendHead(std::string &out, const ResponseWriter &response, std::uint64_t content_length) {
            const std::string status_code = std::to_string(response.metadata.status_code);
            const std::string cl = std::to_string(content_length);
            const std::string_view msg = phraseFor(response.metadata);

            out.append("HTTP/1.1 ");
            out.append(status_code);
            out.append(" ");
            out.append(msg);
            out.append("\r\n");
            for (const auto &h: response.headers.all()) {
                if (h.key == "content-length") continue;
                out.append(h.key);
                out.append(": ");
                out.append(h.value);
                out.append("\r\n");
            }
            out.append("content-length: ");
            out.append(cl);
            out.append("\r\n\r\n");
        }
    }// namespace

    std::string ResponseSerializer::serialize(const ResponseWriter &response) {
        // Headers-only. Used only for the 101 Switching Protocols upgrade
        // response. All other paths (normal + error) go through async
        // send/start+chunk+end.
        std::string rv;
        rv.reserve(64 + response.headers.size() * 32);
        appendHead(rv, response, /*content_length=*/0);
        return rv;
    }

    usub::uvent::task::Awaitable<bool> ResponseSerializer::sendBody(const ResponseWriter &response, std::string body) {
        if (!sink_) {
            context_.state = STATE::DONE;
            co_return false;
        }
        std::string msg;
        msg.reserve(64 + body.size());
        appendHead(msg, response, body.size());
        msg.append(body);
        const bool ok = co_await sink_(std::move(msg));
        context_.state = STATE::DONE;
        co_return ok;
    }

    usub::uvent::task::Awaitable<bool> ResponseSerializer::sendBodyHead(const ResponseWriter &response,
                                                                       std::uint64_t content_length) {
        if (!sink_) {
            context_.state = STATE::DONE;
            co_return false;
        }
        std::string head;
        appendHead(head, response, content_length);
        const bool ok = co_await sink_(std::move(head));
        // DONE: the body that follows is content-length framed, no terminator.
        context_.state = STATE::DONE;
        co_return ok;
    }

    usub::uvent::task::Awaitable<bool> ResponseSerializer::sendHeaders(const ResponseWriter &response) {
        if (context_.state != STATE::INIT) co_return true;
        if (!sink_) {
            context_.state = STATE::DONE;
            co_return false;
        }

        std::string status_line = "HTTP/1.1 ";
        status_line += std::to_string(response.metadata.status_code);
        status_line += ' ';
        status_line.append(phraseFor(response.metadata));
        status_line += "\r\n";

        // Forward user headers; strip the framing headers the chunked path owns.
        for (const auto &h: response.headers) {
            if (h.key == "content-length" || h.key == "transfer-encoding" || h.key == "trailer") continue;
            status_line += h.key;
            status_line += ": ";
            status_line += h.value;
            status_line += "\r\n";
        }
        status_line += "transfer-encoding: chunked\r\n";

        if (response.trailers.size() != 0) {
            status_line += "trailer: ";
            bool first = true;
            for (const auto &h: response.trailers) {
                if (!first) status_line += ", ";
                status_line += h.key;
                first = false;
            }
            status_line += "\r\n";
        }
        status_line += "\r\n";

        const bool ok = co_await sink_(std::move(status_line));
        context_.state = ok ? STATE::HEADERS_SENT : STATE::DONE;
        co_return ok;
    }

    usub::uvent::task::Awaitable<bool> ResponseSerializer::writeChunk(const ResponseWriter &response,
                                                                     std::span<const std::byte> data) {
        if (context_.state == STATE::INIT) {
            if (!co_await sendHeaders(response)) co_return false;
        }
        if (context_.state == STATE::DONE) co_return false;
        if (data.empty()) co_return true;
        if (!sink_) {
            context_.state = STATE::DONE;
            co_return false;
        }

        char len_buf[24];
        const int n = std::snprintf(len_buf, sizeof(len_buf), "%zx\r\n", static_cast<std::size_t>(data.size()));

        std::string frame;
        frame.reserve(static_cast<std::size_t>(n) + data.size() + 2);
        frame.append(len_buf, static_cast<std::size_t>(n));
        frame.append(reinterpret_cast<const char *>(data.data()), data.size());
        frame.append("\r\n", 2);

        const bool ok = co_await sink_(std::move(frame));
        context_.state = ok ? STATE::BODY_WRITING : STATE::DONE;
        co_return ok;
    }

    usub::uvent::task::Awaitable<bool> ResponseSerializer::writeChunkHeader(std::size_t len) {
        if (context_.state == STATE::DONE || !sink_) co_return false;
        char len_buf[24];
        const int n = std::snprintf(len_buf, sizeof(len_buf), "%zx\r\n", len);
        const bool ok = co_await sink_(std::string(len_buf, static_cast<std::size_t>(n)));
        if (ok) context_.state = STATE::BODY_WRITING;
        co_return ok;
    }

    usub::uvent::task::Awaitable<bool> ResponseSerializer::writeChunkTrailer() {
        if (!sink_) co_return false;
        co_return co_await sink_(std::string{"\r\n", 2});
    }

    usub::uvent::task::Awaitable<bool> ResponseSerializer::end(const ResponseWriter &response) {
        if (context_.state == STATE::DONE) co_return true;
        if (context_.state == STATE::INIT) {
            if (!co_await sendHeaders(response)) co_return false;
        }
        if (!sink_) {
            context_.state = STATE::DONE;
            co_return false;
        }

        std::string tail = "0\r\n";
        for (const auto &h: response.trailers) {
            tail += h.key;
            tail += ": ";
            tail += h.value;
            tail += "\r\n";
        }
        tail += "\r\n";

        const bool ok = co_await sink_(std::move(tail));
        context_.state = STATE::DONE;
        co_return ok;
    }

}// namespace usub::unet::http::v1
