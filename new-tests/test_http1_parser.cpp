#include <cassert>
#include <cstddef>
#include <expected>
#include <iostream>
#include <string>
#include <string_view>

#include "unet/http/v1/request_parser.hpp"

using usub::unet::http::ParseError;
using usub::unet::http::STATUS_CODE;
using usub::unet::http::VERSION;
using usub::unet::http::v1::RequestParser;
using State = usub::unet::http::v1::RequestParser::STATE;
using usub::unet::http::Request;

namespace {

    struct ParseHarness {
        explicit ParseHarness(std::string input) : data(std::move(input)), view(data), begin(view.begin()) {}

        std::expected<void, ParseError> parse_to(std::size_t end_index) {
            assert(end_index <= view.size());
            auto end = view.begin() + static_cast<std::ptrdiff_t>(end_index);
            return parser.parse(request, begin, end);
        }

        std::string data;
        std::string_view view;
        std::string_view::const_iterator begin;
        Request request{};
        RequestParser parser{};
    };

    std::size_t header_end_index(const std::string &data) {
        std::size_t pos = data.find("\r\n\r\n");
        assert(pos != std::string::npos);
        return pos + 4;
    }

    void expect_error(const std::string &data, STATUS_CODE expected_status) {
        RequestParser parser;
        Request request;

        std::string_view view(data);
        auto begin = view.begin();
        auto end = view.end();

        // progress guard
        auto last_begin = begin;
        int iters = 0;

        while (true) {
            auto result = parser.parse(request, begin, end);
            const auto st = parser.getContext().state;

            if (!result) {
                // Must be a real failure
                assert(st == State::FAILED);
                assert(result.error().expected_status == expected_status);
                return;
            }

            if (st == State::COMPLETE) {
                // Parser accepted what should have been rejected
                assert(false && "Parser reached COMPLETE but error was expected");
                return;
            }

            // If we consumed nothing and didn't reach a terminal state, we're stuck.
            if (begin == last_begin) {
                // If no more input is available, we can’t make progress -> bug or wrong test.
                assert(false && "Parser made no progress (would loop forever / needs more data)");
                return;
            }

            last_begin = begin;

            // optional hard guard
            if (++iters > 100000) {
                assert(false && "Too many iterations in expect_error()");
                return;
            }
        }
    }


    void expect_error_with_request(const std::string &data, Request &request, STATUS_CODE expected_status) {
        RequestParser parser;

        std::string_view view(data);
        auto begin = view.begin();
        auto end = view.end();

        auto last_begin = begin;
        int iters = 0;

        while (true) {
            auto result = parser.parse(request, begin, end);
            const auto st = parser.getContext().state;

            if (!result) {
                assert(st == State::FAILED);
                assert(result.error().expected_status == expected_status);
                return;
            }

            if (st == State::COMPLETE) {
                assert(false && "Parser reached COMPLETE but error was expected");
                return;
            }

            if (begin == last_begin) {
                assert(false && "Parser made no progress while error was expected");
                return;
            }

            last_begin = begin;

            if (++iters > 100000) {
                assert(false && "Too many iterations in expect_error_with_request()");
                return;
            }
        }
    }


    void test_metadata_and_headers() {
        std::string data = "GET /path?x=1 HTTP/1.1\r\n"
                           "Host: example\r\n"
                           "User-Agent: test\r\n"
                           "\r\n";

        ParseHarness harness(data);
        std::size_t line_end = data.find("\r\n");
        assert(line_end != std::string::npos);

        auto result = harness.parse_to(line_end + 1);
        assert(result);
        assert(harness.parser.getContext().state == State::REQUEST_LINE_CRLF);

        result = harness.parse_to(data.size());
        assert(result);
        assert(harness.parser.getContext().state == State::HEADERS_DONE);

        assert(harness.request.metadata.method_token == "GET");
        assert(harness.request.metadata.uri.path == "/path");
        assert(harness.request.metadata.uri.query == "x=1");
        assert(harness.request.metadata.version == VERSION::HTTP_1_1);

        auto host = harness.request.headers.value("host");
        assert(host.has_value());
        assert(host.value() == "example");

        auto agent = harness.request.headers.value("user-agent");
        assert(agent.has_value());
        assert(agent.value() == "test");
    }

    void test_content_length_body_partial() {
        std::string data = "POST /submit HTTP/1.1\r\n"
                           "Host: example\r\n"
                           "Content-Length: 5\r\n"
                           "\r\n"
                           "Hello";

        ParseHarness harness(data);
        std::size_t header_end = header_end_index(data);

        auto result = harness.parse_to(header_end);
        assert(result);
        assert(harness.parser.getContext().state == State::HEADERS_DONE);

        result = harness.parse_to(header_end + 2);
        assert(result);
        assert(harness.parser.getContext().state == State::DATA_CONTENT_LENGTH);

        result = harness.parse_to(data.size());
        assert(result);
        assert(harness.parser.getContext().state == State::COMPLETE);
        assert(harness.request.body == "Hello");
    }

    void test_chunked_states() {
        std::string data = "POST /chunk HTTP/1.1\r\n"
                           "Host: example\r\n"
                           "Transfer-Encoding: chunked\r\n"
                           "\r\n"
                           "5\r\n"
                           "Hello\r\n";

        ParseHarness harness(data);
        std::size_t header_end = header_end_index(data);

        auto result = harness.parse_to(header_end);
        assert(result);
        assert(harness.parser.getContext().state == State::HEADERS_DONE);

        result = harness.parse_to(header_end + 2);
        assert(result);
        assert(harness.parser.getContext().state == State::DATA_CHUNKED_SIZE_CRLF);

        result = harness.parse_to(header_end + 3);
        assert(result);
        assert(harness.parser.getContext().state == State::DATA_CHUNKED_DATA);

        result = harness.parse_to(header_end + 3 + 5);
        assert(result);
        assert(harness.parser.getContext().state == State::DATA_CHUNKED_DATA_CR);

        result = harness.parse_to(header_end + 3 + 5 + 1);
        assert(result);
        assert(harness.parser.getContext().state == State::DATA_CHUNKED_DATA_LF);

        result = harness.parse_to(header_end + 3 + 5 + 2);
        assert(result);
        assert(harness.parser.getContext().state == State::DATA_CHUNK_DONE);
    }

    void test_chunked_last_and_data_done() {
        {
            RequestParser parser;
            Request request;
            parser.getContext().state = State::DATA_CHUNKED_LAST_CR;

            std::string data = "\r\n";
            std::string_view view(data);
            auto begin = view.begin();
            auto end = view.end();
            auto result = parser.parse(request, begin, end);
            assert(result);
            assert(parser.getContext().state == State::DATA_DONE);
        }

        {
            RequestParser parser;
            Request request;
            parser.getContext().state = State::DATA_DONE;

            std::string data = "x";
            std::string_view view(data);
            auto begin = view.begin();
            auto end = view.end();
            auto result = parser.parse(request, begin, end);
            assert(!result);
            assert(result.error().expected_status == STATUS_CODE::BAD_REQUEST);
        }
    }

    void test_error_cases() {
        expect_error(" / HTTP/1.1\r\nHost: example\r\n\r\n", STATUS_CODE::BAD_REQUEST);
        expect_error("G@T / HTTP/1.1\r\nHost: example\r\n\r\n", STATUS_CODE::BAD_REQUEST);
        expect_error("OPTIONS * HTTP/1.1\r\nHost: example\r\n\r\n", STATUS_CODE::BAD_REQUEST);
        expect_error("GET http://example.com/ HTTP/1.1\r\nHost: example\r\n\r\n", STATUS_CODE::BAD_REQUEST);
        expect_error("GET /bad^ HTTP/1.1\r\nHost: example\r\n\r\n", STATUS_CODE::BAD_REQUEST);
        expect_error("GET /path#frag HTTP/1.1\r\nHost: example\r\n\r\n", STATUS_CODE::BAD_REQUEST);
        expect_error("GET /path?x={ HTTP/1.1\r\nHost: example\r\n\r\n", STATUS_CODE::BAD_REQUEST);
        expect_error("GET / HTTP/1.1X\r\nHost: example\r\n\r\n", STATUS_CODE::BAD_REQUEST);
        expect_error("GET / HTTP/1.111\r\nHost: example\r\n\r\n", STATUS_CODE::BAD_REQUEST);
        expect_error("GET / HTTP/1.1\rHost: example\r\n\r\n", STATUS_CODE::BAD_REQUEST);

        expect_error("GET / HTTP/1.1\r\nHost: example\x7f\r\n\r\n", STATUS_CODE::BAD_REQUEST);

        expect_error("GET / HTTP/1.1\r\n"
                     "Host: example\r\n"
                     "Bad Header: value\r\n"
                     "\r\n",
                     STATUS_CODE::BAD_REQUEST);

        {
            Request request;
            usub::unet::http::max_headers_size = 5;
            expect_error_with_request("GET / HTTP/1.1\r\nHost: example\r\n\r\n", request,
                                      STATUS_CODE::REQUEST_HEADER_FIELDS_TOO_LARGE);
        }

        usub::unet::http::max_headers_size = 256 * 1024;
        // expect_error("POST / HTTP/1.1\r\nContent-Length: abc\r\n\r\n", STATUS_CODE::BAD_REQUEST);
        // expect_error(
        //         "POST / HTTP/1.1\r\n"
        //         "Content-Length: 5\r\n"
        //         "Content-Length: 6\r\n"
        //         "\r\n",
        //         STATUS_CODE::BAD_REQUEST);

        expect_error("POST / HTTP/1.0\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n", STATUS_CODE::BAD_REQUEST);
        expect_error("POST / HTTP/1.1\r\nTransfer-Encoding: gzip\r\n\r\nData", STATUS_CODE::BAD_REQUEST);
        expect_error("POST / HTTP/1.1\r\nTransfer-Encoding: gzip, chunked\r\n\r\n0\r\n\r\n", STATUS_CODE::BAD_REQUEST);
        expect_error("POST / HTTP/1.1\r\n"
                     "Transfer-Encoding: chunked\r\n"
                     "Content-Length: 5\r\n"
                     "\r\n"
                     "Hello",
                     STATUS_CODE::BAD_REQUEST);

        expect_error("GET / HTTP/1.1\r\n"
                     "Content-Length: 5\r\n"
                     "\r\n"
                     "Hello",
                     STATUS_CODE::BAD_REQUEST);

        {
            Request request;
            request.policy.max_body_size = 2;
            expect_error_with_request("POST / HTTP/1.1\r\n"
                                      "Content-Length: 5\r\n"
                                      "\r\n"
                                      "Hello",
                                      request, STATUS_CODE::BAD_REQUEST);
        }
    }

}// namespace

int main() {
    test_metadata_and_headers();
    test_content_length_body_partial();
    test_chunked_states();
    test_chunked_last_and_data_done();
    test_error_cases();

    std::cout << "All http1 parser tests passed.\n";
    return 0;
}
