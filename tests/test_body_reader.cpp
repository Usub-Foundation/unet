#include <cassert>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <uvent/Uvent.h>
#include <uvent/system/SystemContext.h>

#include "unet/http/core/request.hpp"

using usub::unet::http::BODY_ERROR;
using usub::unet::http::RequestReader;

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

    std::string bytesOf(std::string_view s) { return std::string{s}; }

    void collectAcrossPushes() {
        std::cout << "collectAcrossPushes\n";

        usub::Uvent uv{1};
        RequestReader reader;
        std::string collected;
        bool ok = false;
        bool at_eof = false;

        usub::uvent::system::co_spawn_static(
                [&]() -> usub::uvent::task::Awaitable<void> {
                    auto &chan = reader.getBodyChannel();
                    co_await chan.push(bytesOf("hello "));
                    co_await chan.push(bytesOf("world"));
                    chan.close();
                    auto r = co_await reader.collect(1024);
                    if (r) {
                        collected = std::move(*r);
                        ok        = true;
                    }
                    at_eof = reader.eof();
                    uv.stop();
                    co_return;
                }(),
                0);
        uv.run();

        check(ok,                          "collect returned a value");
        check(collected == "hello world",  "collected bytes == hello world");
        check(at_eof,                      "reader at EOF after collect");
    }

    void readFillsCallerBuffer() {
        std::cout << "readFillsCallerBuffer\n";

        usub::Uvent uv{1};
        RequestReader reader;
        std::vector<std::byte> dst(7);
        std::size_t filled1 = 0, filled2 = 0;
        bool ok1 = false, ok2 = false;

        usub::uvent::system::co_spawn_static(
                [&]() -> usub::uvent::task::Awaitable<void> {
                    auto &chan = reader.getBodyChannel();
                    co_await chan.push(bytesOf("0123456789"));
                    chan.close();

                    auto r1 = co_await reader.readBodyBytes(std::span<std::byte>{dst});
                    if (r1) { filled1 = *r1; ok1 = true; }
                    // Second read drains the 3-byte residual and hits EOF.
                    auto r2 = co_await reader.readBodyBytes(std::span<std::byte>{dst});
                    if (r2) { filled2 = *r2; ok2 = true; }
                    uv.stop();
                    co_return;
                }(),
                0);
        uv.run();

        check(ok1 && filled1 == 7, "first read filled 7 bytes");
        check(ok2 && filled2 == 3, "second read drained 3 residual bytes and hit EOF");
    }

    void collectRespectsLimit() {
        std::cout << "collectRespectsLimit\n";

        usub::Uvent uv{1};
        RequestReader reader;
        bool got_error      = false;
        BODY_ERROR err_code = BODY_ERROR::NONE;

        usub::uvent::system::co_spawn_static(
                [&]() -> usub::uvent::task::Awaitable<void> {
                    auto &chan = reader.getBodyChannel();
                    co_await chan.push(bytesOf("aaaaaaaaaa"));// 10 bytes
                    chan.close();
                    auto r = co_await reader.collect(4);// cap < bytes
                    if (!r) {
                        got_error = true;
                        err_code  = r.error();
                    }
                    uv.stop();
                    co_return;
                }(),
                0);
        uv.run();

        check(got_error,                                "collect returned error past the cap");
        check(err_code == BODY_ERROR::FRAME_SIZE_ERROR, "error code is FRAME_SIZE_ERROR");
    }

    void readBodyDrainsEverything() {
        std::cout << "readBodyDrainsEverything\n";

        usub::Uvent uv{1};
        RequestReader reader;
        std::string collected;
        bool ok = false;

        usub::uvent::system::co_spawn_static(
                [&]() -> usub::uvent::task::Awaitable<void> {
                    auto &chan = reader.getBodyChannel();
                    co_await chan.push(bytesOf("alpha "));
                    co_await chan.push(bytesOf("beta "));
                    co_await chan.push(bytesOf("gamma"));
                    chan.close();
                    auto r = co_await reader.readBody();
                    if (r) {
                        collected = std::move(*r);
                        ok        = true;
                    }
                    uv.stop();
                    co_return;
                }(),
                0);
        uv.run();

        check(ok,                                "readBody returned a value");
        check(collected == "alpha beta gamma",   "readBody concatenated every chunk");
    }

    void engineSignalsError() {
        std::cout << "engineSignalsError\n";

        usub::Uvent uv{1};
        RequestReader reader;
        bool got_error      = false;
        BODY_ERROR err_code = BODY_ERROR::NONE;

        usub::uvent::system::co_spawn_static(
                [&]() -> usub::uvent::task::Awaitable<void> {
                    auto &chan = reader.getBodyChannel();
                    co_await chan.push(bytesOf("partial"));
                    chan.signalError(BODY_ERROR::PROTOCOL_ERROR);

                    // Drain the good chunk first.
                    (void) co_await reader.chunk();
                    // Next pop sees the error.
                    auto r = co_await reader.chunk();
                    if (!r) {
                        got_error = true;
                        err_code  = r.error();
                    }
                    uv.stop();
                    co_return;
                }(),
                0);
        uv.run();

        check(got_error,                              "second chunk() observed the error");
        check(err_code == BODY_ERROR::PROTOCOL_ERROR, "error code propagated as PROTOCOL_ERROR");
    }

    void readBodyBytesN() {
        std::cout << "readBodyBytesN\n";

        usub::Uvent uv{1};
        RequestReader reader;
        std::string first, second;
        bool ok1 = false, ok2 = false;

        usub::uvent::system::co_spawn_static(
                [&]() -> usub::uvent::task::Awaitable<void> {
                    auto &chan = reader.getBodyChannel();
                    co_await chan.push(bytesOf("0123456789"));
                    chan.close();

                    auto r1 = co_await reader.readBodyBytes(7);
                    if (r1) { first = std::move(*r1); ok1 = true; }
                    // Second call drains the 3-byte residual, short return == EOF.
                    auto r2 = co_await reader.readBodyBytes(7);
                    if (r2) { second = std::move(*r2); ok2 = true; }
                    uv.stop();
                    co_return;
                }(),
                0);
        uv.run();

        check(ok1 && first == "0123456",  "readBodyBytes(7) returned first 7 bytes");
        check(ok2 && second == "789",     "readBodyBytes(7) drained residual, short at EOF");
    }

    void mixedBytesThenChunk() {
        std::cout << "mixedBytesThenChunk\n";

        usub::Uvent uv{1};
        RequestReader reader;
        std::size_t filled = 0;
        std::string tail;
        bool ok_read = false, ok_chunk = false;

        usub::uvent::system::co_spawn_static(
                [&]() -> usub::uvent::task::Awaitable<void> {
                    auto &chan = reader.getBodyChannel();
                    co_await chan.push(bytesOf("0123456789"));
                    chan.close();

                    std::vector<std::byte> dst(7);
                    auto r1 = co_await reader.readBodyBytes(std::span<std::byte>{dst});
                    if (r1) { filled = *r1; ok_read = true; }
                    // chunk() must hand back ONLY the unconsumed residual ("789"),
                    // not replay the 7 bytes readBodyBytes() already returned.
                    auto r2 = co_await reader.chunk();
                    if (r2 && r2->has_value()) { tail = std::move(**r2); ok_chunk = true; }
                    uv.stop();
                    co_return;
                }(),
                0);
        uv.run();

        check(ok_read && filled == 7,        "readBodyBytes(buffer) filled 7 bytes");
        check(ok_chunk && tail == "789",     "chunk() returned only the residual, no replay");
    }

}// namespace

int main() {
    collectAcrossPushes();
    readFillsCallerBuffer();
    collectRespectsLimit();
    readBodyDrainsEverything();
    engineSignalsError();
    readBodyBytesN();
    mixedBytesThenChunk();

    std::cout << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
