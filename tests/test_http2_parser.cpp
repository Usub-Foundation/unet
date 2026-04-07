#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

// #include "unet/http/core/response.hpp"
// #include "unet/http/v2/connection_parser.hpp"
// #include "unet/http/v2/frames.hpp"
// #include "unet/http/v2/hpack.hpp"
// #include "unet/http/v2/response_serializer.hpp"
// #include "unet/http/v2/stream.hpp"

// using usub::unet::http::Response;
// using usub::unet::http::VERSION;
// using usub::unet::http::v2::ConnectionParser;
// using usub::unet::http::v2::FRAME_TYPE;
// using usub::unet::http::v2::FLAGS;
// using usub::unet::http::v2::FrameHeader;
// using usub::unet::http::v2::ResponseSerializer;
// using usub::unet::http::v2::Stream;
// using usub::unet::http::v2::hpack::Decoder;
// using usub::unet::http::v2::hpack::Encoder;
// using usub::unet::http::v2::hpack::HeaderField;
// using usub::unet::http::v2::hpack::Indexing;

// namespace {
//     std::string encode_headers(Encoder &encoder, std::initializer_list<HeaderField> fields) {
//         std::vector<HeaderField> data(fields.begin(), fields.end());
//         return encoder.encode(data, false);
//     }

//     std::string build_frame_bytes(FRAME_TYPE type, std::uint8_t flags, std::uint32_t stream_id,
//                                   std::string_view payload) {
//         const std::size_t payload_size = payload.size();
//         std::string frame;
//         frame.resize(usub::unet::http::v2::frame_header_size + payload_size);
//         frame[0] = static_cast<char>((payload_size >> 16) & 0xff);
//         frame[1] = static_cast<char>((payload_size >> 8) & 0xff);
//         frame[2] = static_cast<char>(payload_size & 0xff);
//         frame[3] = static_cast<char>(type);
//         frame[4] = static_cast<char>(flags);
//         frame[5] = static_cast<char>((stream_id >> 24) & 0x7f);
//         frame[6] = static_cast<char>((stream_id >> 16) & 0xff);
//         frame[7] = static_cast<char>((stream_id >> 8) & 0xff);
//         frame[8] = static_cast<char>(stream_id & 0xff);
//         if (!payload.empty()) {
//             std::memcpy(frame.data() + usub::unet::http::v2::frame_header_size, payload.data(), payload_size);
//         }
//         return frame;
//     }

//     void test_connection_parser_preface_settings_split() {
//         ConnectionParser parser;

//         std::string preface(usub::unet::http::v2::preface);
//         std::string part1 = preface.substr(0, 7);
//         std::string part2 = preface.substr(7);

//         auto result = parser.consume(part1);
//         assert(result.has_value());
//         assert(result->empty());

//         std::string settings_frame = build_frame_bytes(FRAME_TYPE::SETTINGS, 0x00, 0, {});
//         std::string chunk = part2 + settings_frame;

//         result = parser.consume(chunk);
//         assert(result.has_value());
//         assert(result->size() == 1);
//         assert(result->at(0).header.type == FRAME_TYPE::SETTINGS);
//         assert(parser.state() == ConnectionParser::STATE::READY);
//     }

//     void test_stream_headers_and_data() {
//         Stream stream(1);
//         Decoder decoder;
//         Encoder encoder;

//         const std::string block = encode_headers(encoder, {
//                                                                   {":method", "GET", Indexing::None},
//                                                                   {":path", "/hello?x=1#frag", Indexing::None},
//                                                                   {":scheme", "https", Indexing::None},
//                                                                   {":authority", "example.com:443", Indexing::None},
//                                                                   {"user-agent", "test", Indexing::None},
//                                                           });

//         FrameHeader headers{};
//         headers.type = FRAME_TYPE::HEADERS;
//         headers.flags = static_cast<std::uint8_t>(FLAGS::END_HEADERS);
//         headers.length = static_cast<std::uint32_t>(block.size());
//         headers.stream_identifier = 1;

//         auto result = stream.on_frame(headers, block, decoder);
//         assert(result.result == Stream::RESULT::HEADERS_DONE);
//         assert(stream.request().metadata.method_token == "GET");
//         assert(stream.request().metadata.uri.path == "/hello");
//         assert(stream.request().metadata.uri.query == "x=1");
//         assert(stream.request().metadata.uri.fragment == "frag");
//         assert(stream.request().metadata.uri.scheme == "https");
//         assert(stream.request().metadata.authority == "example.com:443");
//         assert(stream.request().metadata.uri.authority.host == "example.com");
//         assert(stream.request().metadata.uri.authority.port == 443);

//         const auto agent = stream.request().headers.value("user-agent");
//         assert(agent.has_value());
//         assert(agent.value() == "test");

//         const std::string data = "abc";
//         FrameHeader data_header{};
//         data_header.type = FRAME_TYPE::DATA;
//         data_header.flags = static_cast<std::uint8_t>(FLAGS::END_STREAM);
//         data_header.length = static_cast<std::uint32_t>(data.size());
//         data_header.stream_identifier = 1;

//         result = stream.on_frame(data_header, data, decoder);
//         assert(result.result == Stream::RESULT::REQUEST_DONE);
//         assert(stream.request().body == "abc");
//     }

//     void test_stream_continuation_headers() {
//         Stream stream(3);
//         Decoder decoder;
//         Encoder encoder;

//         const std::string block = encode_headers(encoder, {
//                                                                   {":method", "GET", Indexing::None},
//                                                                   {":path", "/", Indexing::None},
//                                                                   {":scheme", "https", Indexing::None},
//                                                                   {":authority", "example.com", Indexing::None},
//                                                           });

//         std::string first = block.substr(0, block.size() / 2);
//         std::string second = block.substr(block.size() / 2);

//         FrameHeader headers{};
//         headers.type = FRAME_TYPE::HEADERS;
//         headers.flags = 0x00;
//         headers.length = static_cast<std::uint32_t>(first.size());
//         headers.stream_identifier = 3;

//         auto result = stream.on_frame(headers, first, decoder);
//         assert(result.result == Stream::RESULT::NONE);
//         assert(stream.header_block_in_progress());

//         FrameHeader continuation{};
//         continuation.type = FRAME_TYPE::CONTINUATION;
//         continuation.flags = static_cast<std::uint8_t>(FLAGS::END_HEADERS);
//         continuation.length = static_cast<std::uint32_t>(second.size());
//         continuation.stream_identifier = 3;

//         result = stream.on_frame(continuation, second, decoder);
//         assert(result.result == Stream::RESULT::HEADERS_DONE);
//         assert(!stream.header_block_in_progress());
//         assert(stream.request().metadata.uri.path == "/");
//     }

//     void test_response_serializer_headers() {
//         Response response;
//         response.metadata.version = VERSION::HTTP_2_0;
//         response.metadata.status_code = 200;

//         Encoder encoder;
//         const std::string out = ResponseSerializer::serialize_headers(response, encoder, false);
//         Decoder decoder;
//         auto decoded = decoder.decode(out);
//         assert(decoded.has_value());
//         bool saw_status = false;
//         for (const auto &field: decoded.value()) {
//             if (field.name == ":status" && field.value == "200") { saw_status = true; }
//         }
//         assert(saw_status);
//     }
// }// namespace

int main() {
    // test_connection_parser_preface_settings_split();
    // test_stream_headers_and_data();
    // test_stream_continuation_headers();
    // test_response_serializer_headers();

    // std::cout << "All http2 parser/serializer tests passed.\n";
    return 0;
}
