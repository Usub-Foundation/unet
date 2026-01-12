#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "unet/http/response.hpp"
#include "unet/http/v2/hpack.hpp"
#include "unet/http/v2/request_parser.hpp"
#include "unet/http/v2/response_serializer.hpp"

using usub::unet::http::Request;
using usub::unet::http::Response;
using usub::unet::http::VERSION;
using usub::unet::http::v2::RequestParser;
using usub::unet::http::v2::ResponseSerializer;
using usub::unet::http::v2::hpack::Decoder;
using usub::unet::http::v2::hpack::Encoder;
using usub::unet::http::v2::hpack::HeaderField;
using usub::unet::http::v2::hpack::Indexing;

namespace {
    std::string encode_headers(Encoder &encoder, std::initializer_list<HeaderField> fields) {
        std::vector<HeaderField> data(fields.begin(), fields.end());
        return encoder.encode(data, false);
    }

    void test_request_parser_basic() {
        Request request;
        RequestParser parser;
        Decoder decoder;
        Encoder encoder;

        const std::string block = encode_headers(encoder, {
                                                                  {":method", "GET", Indexing::None},
                                                                  {":path", "/hello?x=1#frag", Indexing::None},
                                                                  {":scheme", "https", Indexing::None},
                                                                  {":authority", "example.com:443", Indexing::None},
                                                                  {"user-agent", "test", Indexing::None},
                                                          });

        const auto result = parser.parse_frame(RequestParser::FrameType::HEADERS,
                                               RequestParser::FLAG_END_HEADERS | RequestParser::FLAG_END_STREAM, block,
                                               decoder, request);
        assert(result.state == RequestParser::ResultState::HEADERS_DONE);
        assert(parser.is_complete());
        assert(request.metadata.method_token == "GET");
        assert(request.metadata.uri.path == "/hello");
        assert(request.metadata.uri.query == "x=1");
        assert(request.metadata.uri.fragment == "frag");
        assert(request.metadata.uri.scheme == "https");
        assert(request.metadata.authority == "example.com:443");
        assert(request.metadata.uri.authority.host == "example.com");
        assert(request.metadata.uri.authority.port == 443);

        const auto agent = request.headers.value("user-agent");
        assert(agent.has_value());
        assert(agent.value() == "test");
    }

    void test_request_parser_trailers() {
        Request request;
        RequestParser parser;
        Decoder decoder;
        Encoder encoder;

        const std::string initial = encode_headers(encoder, {
                                                                    {":method", "GET", Indexing::None},
                                                                    {":path", "/", Indexing::None},
                                                            });
        auto result = parser.parse_frame(RequestParser::FrameType::HEADERS, RequestParser::FLAG_END_HEADERS, initial,
                                         decoder, request);
        assert(result.state == RequestParser::ResultState::HEADERS_DONE);

        const std::string data = "abc";
        result = parser.parse_frame(RequestParser::FrameType::DATA, 0, data, decoder, request);
        assert(result.state == RequestParser::ResultState::DATA);

        const std::string ok_block = encode_headers(encoder, {
                                                                     {"x-trace", "abc", Indexing::None},
                                                             });
        result = parser.parse_frame(RequestParser::FrameType::HEADERS,
                                    RequestParser::FLAG_END_HEADERS | RequestParser::FLAG_END_STREAM, ok_block, decoder,
                                    request);
        assert(result.state == RequestParser::ResultState::OK);
        assert(parser.is_complete());

        Request request2;
        RequestParser parser2;
        Decoder decoder2;
        Encoder encoder2;
        const std::string initial2 = encode_headers(encoder2, {
                                                                      {":method", "GET", Indexing::None},
                                                                      {":path", "/", Indexing::None},
                                                              });
        result = parser2.parse_frame(RequestParser::FrameType::HEADERS, RequestParser::FLAG_END_HEADERS, initial2,
                                     decoder2, request2);
        assert(result.state == RequestParser::ResultState::HEADERS_DONE);
        const std::string bad_block = encode_headers(encoder2, {
                                                                       {":method", "GET", Indexing::None},
                                                               });
        result = parser2.parse_frame(RequestParser::FrameType::HEADERS,
                                     RequestParser::FLAG_END_HEADERS | RequestParser::FLAG_END_STREAM, bad_block,
                                     decoder2, request2);
        assert(result.state == RequestParser::ResultState::ERROR);
    }

    void test_response_serializer_headers() {
        Response response;
        response.metadata.version = VERSION::HTTP_2_0;
        response.metadata.status_code = 200;
        // response.headers.addHeader("Content-Type", "text/plain");

        Encoder encoder;
        const std::string out = ResponseSerializer::serialize_headers(response, encoder, false);
        Decoder decoder;
        auto decoded = decoder.decode(out);
        assert(decoded.has_value());
        bool saw_status = false;
        bool saw_type = false;
        for (const auto &field: decoded.value()) {
            if (field.name == ":status" && field.value == "200") { saw_status = true; }
            if (field.name == "content-type" && field.value == "text/plain") { saw_type = true; }
        }
        assert(saw_status);
        assert(saw_type);
    }

}// namespace

int main() {
    test_request_parser_basic();
    test_request_parser_trailers();
    test_response_serializer_headers();

    std::cout << "All http2 parser/serializer tests passed.\n";
    return 0;
}
