#include <cassert>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "unet/http/core/request.hpp"
#include "unet/http/v1/wire/request_parser.hpp"

using usub::unet::http::RequestReader;
using usub::unet::http::v1::RequestParser;

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

    // Drives the parser to completion, collecting body bytes the way the
    // session would. The parser can advance with no input — HEADERS_DONE
    // transitions to the body state / COMPLETE consuming zero bytes — so the
    // loop is gated on completion, not on remaining bytes. A step that changes
    // neither the state nor the cursor means we're out of input mid-message;
    // we stop there to avoid spinning on a truncated request.
    std::string driveAll(RequestParser &parser, RequestReader &reader, std::string_view raw) {
        std::string body;
        auto begin = raw.begin();
        const auto end = raw.end();
        while (parser.getContext().state != RequestParser::STATE::FAILED &&
               parser.getContext().state != RequestParser::STATE::COMPLETE) {
            const auto before_state = parser.getContext().state;
            const auto before_begin = begin;
            auto r = parser.step(reader, begin, end);
            assert(r.has_value());
            body += parser.takePendingBody();
            if (parser.getContext().state == before_state && begin == before_begin) break;
        }
        return body;
    }

    void simpleGet() {
        std::cout << "simpleGet\n";
        RequestParser parser;
        RequestReader reader;
        constexpr std::string_view raw = "GET /hello HTTP/1.1\r\nHost: example.com\r\n\r\n";
        auto body = driveAll(parser, reader, raw);

        using STATE = RequestParser::STATE;
        check(parser.getContext().state > STATE::HEADERS_VALIDATION, "headers parsed after GET");
        check(parser.getContext().state == STATE::COMPLETE,          "state == COMPLETE after GET");
        check(reader.metadata.method_token == "GET", "method == GET");
        check(reader.metadata.uri.path    == "/hello", "path == /hello");
        check(reader.metadata.authority   == "example.com", "Host carried into authority");
        check(body.empty(), "no body for GET");
    }

    void postContentLength() {
        std::cout << "postContentLength\n";
        RequestParser parser;
        RequestReader reader;
        const std::string_view raw =
                "POST /upload HTTP/1.1\r\n"
                "Host: x\r\n"
                "Content-Length: 5\r\n"
                "\r\n"
                "hello";
        auto body = driveAll(parser, reader, raw);

        using STATE = RequestParser::STATE;
        check(parser.getContext().state > STATE::HEADERS_VALIDATION, "headers parsed after POST");
        check(parser.getContext().state == STATE::COMPLETE,          "state == COMPLETE after 5 body bytes");
        check(reader.metadata.method_token == "POST", "method == POST");
        check(body.size() == 5, "5 body bytes delivered");
        check(body == "hello", "body == hello");
    }

    void postChunked() {
        std::cout << "postChunked\n";
        RequestParser parser;
        RequestReader reader;
        const std::string_view raw =
                "POST /upload HTTP/1.1\r\n"
                "Host: x\r\n"
                "Transfer-Encoding: chunked\r\n"
                "\r\n"
                "5\r\nhello\r\n"
                "6\r\n world\r\n"
                "0\r\n\r\n";
        auto body = driveAll(parser, reader, raw);

        using STATE = RequestParser::STATE;
        check(parser.getContext().state == STATE::COMPLETE, "state == COMPLETE after final 0-size chunk");
        check(body.size() == 11, "11 body bytes (hello + space + world)");
        check(body == "hello world", "body == hello world");
    }

    void splitAcrossSteps() {
        // Feeds the parser two bytes at a time to simulate fragmented reads.
        std::cout << "splitAcrossSteps\n";
        RequestParser parser;
        RequestReader reader;
        const std::string_view raw =
                "POST /x HTTP/1.1\r\n"
                "Host: x\r\n"
                "Content-Length: 4\r\n"
                "\r\n"
                "abcd";
        using STATE = RequestParser::STATE;
        std::string body;
        std::size_t offset = 0;
        while (offset < raw.size() && parser.getContext().state != STATE::FAILED) {
            const std::size_t take = std::min<std::size_t>(2, raw.size() - offset);
            std::string_view slice{raw.data() + offset, take};
            auto begin = slice.begin();
            const auto end = slice.end();
            while (begin != end) {
                auto r = parser.step(reader, begin, end);
                assert(r.has_value());
                body += parser.takePendingBody();
            }
            offset += take;
        }
        check(parser.getContext().state == STATE::COMPLETE, "state == COMPLETE despite 2-byte chunks");
        check(body == "abcd", "body reassembled to abcd");
    }

    void badRequestFails() {
        std::cout << "badRequestFails\n";
        RequestParser parser;
        RequestReader reader;
        constexpr std::string_view raw = "GET / HTTP/9.9\r\nHost: x\r\n\r\n";// unknown version
        auto begin = raw.begin();
        const auto end = raw.end();
        bool got_error = false;
        while (begin != end) {
            auto r = parser.step(reader, begin, end);
            if (!r) { got_error = true; break; }
        }
        check(got_error, "step returned an error");
        check(parser.getContext().state == RequestParser::STATE::FAILED,
              "state == FAILED after malformed request");
    }

}// namespace

int main() {
    simpleGet();
    postContentLength();
    postChunked();
    splitAcrossSteps();
    badRequestFails();

    std::cout << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
