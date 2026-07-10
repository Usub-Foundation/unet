#include <cassert>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <uvent/Uvent.h>
#include <uvent/system/SystemContext.h>

#include "unet/http/core/response.hpp"
#include "unet/http/v1/wire/response_serializer.hpp"

using usub::unet::http::ResponseWriter;
using usub::unet::http::v1::ResponseSerializer;
using STATE = ResponseSerializer::STATE;

namespace {

    std::size_t passed = 0;
    std::size_t failed = 0;

    void check(bool cond, const char *what) {
        if (cond) {
            ++passed;
        } else {
            ++failed;
            std::cerr << "  FAIL: " << what << "\n";
        }
    }

    bool contains(const std::string &haystack, std::string_view needle) {
        return haystack.find(needle) != std::string::npos;
    }

    // serialize() is now headers-only — used by the engine for the upgrade
    // 101 response and the parse-error path. Bodies flow through sendBody()
    // (tested separately). Verifies header structure + Content-Length: 0.
    void headersOnlySerialize() {
        std::cout << "headersOnlySerialize\n";
        ResponseWriter w;
        w.metadata.status_code = 200;
        w.headers.addHeader(std::string_view{"content-type"}, std::string_view{"text/plain"});

        const std::string bytes = ResponseSerializer::serialize(w);
        check(bytes.starts_with("HTTP/1.1 200 OK\r\n"), "status line");
        check(contains(bytes, "content-type: text/plain\r\n"), "user header present");
        check(contains(bytes, "content-length: 0\r\n"), "content-length: 0 for headers-only");
        check(bytes.ends_with("\r\n\r\n"), "trailing blank line, no body");
    }

    void customReasonOnHeadersOnly() {
        std::cout << "customReasonOnHeadersOnly\n";
        ResponseWriter w;
        w.metadata.status_code = 418;
        w.metadata.status_message = "I am a coffee pot";

        const std::string bytes = ResponseSerializer::serialize(w);
        check(bytes.starts_with("HTTP/1.1 418 I am a coffee pot\r\n"), "custom reason phrase wins");
    }

    // Drives the entire streaming sequence — sendHeaders → writeChunk → end —
    // in one coroutine on a single Uvent instance. The sink captures bytes per
    // phase by snapshotting and clearing between calls.
    void streamingPath() {
        std::cout << "streamingPath\n";

        usub::Uvent uv{1};

        ResponseWriter w;
        w.metadata.status_code = 200;
        w.headers.addHeader(std::string_view{"content-type"}, std::string_view{"text/plain"});

        std::string captured;
        auto sink = [&](std::string chunk) -> usub::uvent::task::Awaitable<bool> {
            captured.append(chunk);
            co_return true;
        };

        ResponseSerializer serializer{sink};

        STATE       s_init{}, s_after_headers{}, s_after_write{}, s_after_end{};
        bool        ok_headers = false, ok_write = false, ok_end = false;
        std::string header_bytes, chunk_bytes, end_bytes;

        usub::uvent::system::co_spawn_static(
                [&]() -> usub::uvent::task::Awaitable<void> {
                    s_init = serializer.getContext().state;

                    ok_headers      = co_await serializer.sendHeaders(w);
                    s_after_headers = serializer.getContext().state;
                    header_bytes    = captured;
                    captured.clear();

                    const std::string chunk = "hello";
                    const std::byte  *p     = reinterpret_cast<const std::byte *>(chunk.data());
                    ok_write       = co_await serializer.writeChunk(w, std::span<const std::byte>{p, chunk.size()});
                    s_after_write  = serializer.getContext().state;
                    chunk_bytes    = captured;
                    captured.clear();

                    ok_end      = co_await serializer.end(w);
                    s_after_end = serializer.getContext().state;
                    end_bytes   = captured;

                    uv.stop();
                    co_return;
                }(),
                0);
        uv.run();

        check(s_init == STATE::INIT, "starts in INIT");

        check(ok_headers,                                "sendHeaders succeeded");
        check(s_after_headers == STATE::HEADERS_SENT,    "state == HEADERS_SENT");
        check(header_bytes.starts_with("HTTP/1.1 200 OK\r\n"),   "status line on wire");
        check(contains(header_bytes, "transfer-encoding: chunked\r\n"), "TE: chunked added");
        check(!contains(header_bytes, "content-length"), "no content-length in streaming");
        check(header_bytes.ends_with("\r\n\r\n"),        "headers terminated by blank line");

        check(ok_write,                                  "writeChunk succeeded");
        check(s_after_write == STATE::BODY_WRITING,      "state == BODY_WRITING");
        check(chunk_bytes == "5\r\nhello\r\n",           "chunk frame: 5\\r\\nhello\\r\\n");

        check(ok_end,                                    "end succeeded");
        check(s_after_end == STATE::DONE,                "state == DONE");
        check(end_bytes == "0\r\n\r\n",                  "terminating 0\\r\\n\\r\\n");
    }

    // If the handler skips send_headers() and calls writeChunk() first, the
    // serializer auto-flushes the headers before the chunk.
    void writeAutoFlushesHeaders() {
        std::cout << "writeAutoFlushesHeaders\n";

        usub::Uvent uv{1};

        ResponseWriter w;
        w.metadata.status_code = 200;
        std::string captured;
        auto sink = [&](std::string chunk) -> usub::uvent::task::Awaitable<bool> {
            captured.append(chunk);
            co_return true;
        };

        ResponseSerializer serializer{sink};

        STATE post_state{};
        bool  ok = false;

        usub::uvent::system::co_spawn_static(
                [&]() -> usub::uvent::task::Awaitable<void> {
                    const std::string body = "x";
                    const std::byte  *p    = reinterpret_cast<const std::byte *>(body.data());
                    ok         = co_await serializer.writeChunk(w, std::span<const std::byte>{p, 1});
                    post_state = serializer.getContext().state;
                    uv.stop();
                    co_return;
                }(),
                0);
        uv.run();

        check(ok,                                  "writeChunk auto-fired sendHeaders");
        check(post_state == STATE::BODY_WRITING,   "state == BODY_WRITING");
        check(contains(captured, "HTTP/1.1 200"),  "status line emitted by auto-flush");
        check(captured.ends_with("1\r\nx\r\n"),    "chunk frame appended after headers");
    }

}// namespace

int main() {
    headersOnlySerialize();
    customReasonOnHeadersOnly();
    streamingPath();
    writeAutoFlushesHeaders();

    std::cout << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
