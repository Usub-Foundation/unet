// WebSocket frame parser / serializer unit tests.
// No network, no Uvent — runs immediately.
// Build with: cmake -DUNET_TESTS=ON, then: ./test_ws_parser

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>

#include "unet/ws/core/frame.hpp"
#include "unet/ws/core/handshake.hpp"
#include "unet/ws/wire/client_frame_parser.hpp"
#include "unet/ws/wire/server_frame_parser.hpp"
#include "unet/ws/wire/client_frame_serializer.hpp"
#include "unet/ws/wire/server_frame_serializer.hpp"

using namespace usub::unet;

// ─── helpers ────────────────────────────────────────────────────────────────

// Feed the full byte string into a ClientFrameParser in one shot.
// Returns the parse result (true = complete).
static std::expected<bool, FrameParseError>
feed_client(ClientFrameParser &parser, ClientFrame &frame, const std::string &bytes) {
    std::string_view view{bytes};
    auto begin = view.begin();
    return parser.step(frame, begin, view.end());
}

// Build a minimal masked client frame manually (TEXT, single-byte mask key 0x00).
static std::string make_masked_frame(uint8_t opcode, std::string_view payload,
                                     std::array<uint8_t,4> mask = {0x37,0xfa,0x21,0x3d}) {
    ClientFrame f;
    f.fin      = true;
    f.opcode   = opcode;
    f.mask_key = mask;
    f.payload  = std::string(payload);
    return ClientFrameSerializer::serialize(f);
}

// Build an unmasked server frame manually.
static std::string make_server_frame(uint8_t opcode, std::string_view payload) {
    ServerFrame f;
    f.fin     = true;
    f.opcode  = opcode;
    f.payload = std::string(payload);
    return ServerFrameSerializer::serialize(f);
}

// ─── ClientFrameParser tests ─────────────────────────────────────────────────

static void test_parse_short_text_frame() {
    const std::array<uint8_t,4> key = {0x37, 0xfa, 0x21, 0x3d};
    const std::string plain = "Hello";

    const std::string wire = make_masked_frame(static_cast<uint8_t>(Opcode::TEXT), plain, key);

    ClientFrameParser parser;
    ClientFrame frame;
    auto result = feed_client(parser, frame, wire);

    assert(result && *result && "frame should be complete");
    assert(frame.fin);
    assert(frame.opcode == static_cast<uint8_t>(Opcode::TEXT));
    assert(frame.mask_key == key);
    assert(frame.payload == plain);

    std::cout << "[PASS] parse_short_text_frame\n";
}

static void test_parse_empty_payload_frame() {
    const std::string wire = make_masked_frame(static_cast<uint8_t>(Opcode::PING), "");

    ClientFrameParser parser;
    ClientFrame frame;
    auto result = feed_client(parser, frame, wire);

    assert(result && *result);
    assert(frame.opcode == static_cast<uint8_t>(Opcode::PING));
    assert(frame.payload.empty());

    std::cout << "[PASS] parse_empty_payload_frame\n";
}

static void test_parse_two_frames_sequential() {
    const std::array<uint8_t,4> key = {0x01, 0x02, 0x03, 0x04};
    const std::string wire =
        make_masked_frame(static_cast<uint8_t>(Opcode::TEXT), "foo", key) +
        make_masked_frame(static_cast<uint8_t>(Opcode::BINARY), "bar", key);

    std::string_view view{wire};
    auto begin = view.begin();
    const auto end = view.end();

    ClientFrameParser parser;
    ClientFrame frame;

    auto r1 = parser.step(frame, begin, end);
    assert(r1 && *r1);
    assert(frame.payload == "foo");
    parser.reset();

    frame = {};
    auto r2 = parser.step(frame, begin, end);
    assert(r2 && *r2);
    assert(frame.payload == "bar");
    assert(frame.opcode == static_cast<uint8_t>(Opcode::BINARY));

    std::cout << "[PASS] parse_two_frames_sequential\n";
}

static void test_parse_incremental_byte_by_byte() {
    const std::string wire = make_masked_frame(static_cast<uint8_t>(Opcode::TEXT), "Hi");

    ClientFrameParser parser;
    ClientFrame frame;
    bool done = false;

    for (std::size_t i = 0; i < wire.size(); ++i) {
        std::string_view chunk{wire.data() + i, 1};
        auto begin = chunk.begin();
        auto result = parser.step(frame, begin, chunk.end());
        assert(result.has_value() && "no parse error expected");
        if (*result) { done = true; break; }
    }

    assert(done);
    assert(frame.payload == "Hi");

    std::cout << "[PASS] parse_incremental_byte_by_byte\n";
}

static void test_rejects_unmasked_client_frame() {
    // Build a valid TEXT frame bytes, then clear the MASK bit (byte 1).
    const std::string wire = make_masked_frame(static_cast<uint8_t>(Opcode::TEXT), "secret");
    std::string bad = wire;
    bad[1] &= ~0x80u;  // clear MASK bit

    ClientFrameParser parser;
    ClientFrame frame;
    auto result = feed_client(parser, frame, bad);

    assert(!result && "should return error");
    assert(result.error().code == FrameParseError::CODE::UNMASKED_CLIENT_FRAME);
    assert(parser.getContext().state == ClientFrameParser::STATE::FAILED);

    std::cout << "[PASS] rejects_unmasked_client_frame\n";
}

static void test_rejects_reserved_bits() {
    std::string wire = make_masked_frame(static_cast<uint8_t>(Opcode::TEXT), "x");
    wire[0] |= 0x40u;  // set RSV1

    ClientFrameParser parser;
    ClientFrame frame;
    auto result = feed_client(parser, frame, wire);

    assert(!result);
    assert(result.error().code == FrameParseError::CODE::RESERVED_BITS_SET);

    std::cout << "[PASS] rejects_reserved_bits\n";
}

static void test_rejects_fragmented_control_frame() {
    std::string wire = make_masked_frame(static_cast<uint8_t>(Opcode::PING), "");
    wire[0] &= ~0x80u;  // clear FIN bit → fragmented control

    ClientFrameParser parser;
    ClientFrame frame;
    auto result = feed_client(parser, frame, wire);

    assert(!result);
    assert(result.error().code == FrameParseError::CODE::CONTROL_FRAME_FRAGMENTED);

    std::cout << "[PASS] rejects_fragmented_control_frame\n";
}

// ─── ServerFrameParser tests ─────────────────────────────────────────────────

static void test_server_parser_accepts_unmasked() {
    const std::string wire = make_server_frame(static_cast<uint8_t>(Opcode::TEXT), "world");

    ServerFrameParser parser;
    ServerFrame frame;
    std::string_view view{wire};
    auto begin = view.begin();
    auto result = parser.step(frame, begin, view.end());

    assert(result && *result);
    assert(frame.payload == "world");
    assert(frame.opcode == static_cast<uint8_t>(Opcode::TEXT));

    std::cout << "[PASS] server_parser_accepts_unmasked\n";
}

static void test_server_parser_rejects_masked() {
    // Build a masked frame and present it to ServerFrameParser.
    const std::string wire = make_masked_frame(static_cast<uint8_t>(Opcode::TEXT), "hi");

    ServerFrameParser parser;
    ServerFrame frame;
    std::string_view view{wire};
    auto begin = view.begin();
    auto result = parser.step(frame, begin, view.end());

    assert(!result);
    assert(result.error().code == FrameParseError::CODE::MASKED_SERVER_FRAME);

    std::cout << "[PASS] server_parser_rejects_masked\n";
}

// ─── Serializer tests ────────────────────────────────────────────────────────

static void test_server_serializer_mask_bit_clear() {
    const std::string wire = ServerFrameSerializer::text("hello");
    // Byte 1 must not have the MASK bit set (0x80).
    assert((static_cast<uint8_t>(wire[1]) & 0x80u) == 0u);
    assert((static_cast<uint8_t>(wire[1]) & 0x7Fu) == 5u);  // payload length 5

    std::cout << "[PASS] server_serializer_mask_bit_clear\n";
}

static void test_client_serializer_mask_bit_set_and_roundtrip() {
    const std::array<uint8_t,4> key = {0xAB, 0xCD, 0xEF, 0x01};
    const std::string wire = ClientFrameSerializer::text("Hello", key);

    // Byte 1: MASK bit set, length = 5.
    assert((static_cast<uint8_t>(wire[1]) & 0x80u) != 0u);
    assert((static_cast<uint8_t>(wire[1]) & 0x7Fu) == 5u);

    // Bytes 2–5: masking key.
    assert(static_cast<uint8_t>(wire[2]) == key[0]);
    assert(static_cast<uint8_t>(wire[3]) == key[1]);
    assert(static_cast<uint8_t>(wire[4]) == key[2]);
    assert(static_cast<uint8_t>(wire[5]) == key[3]);

    // Feed back through the parser — should recover "Hello".
    ClientFrameParser parser;
    ClientFrame frame;
    std::string_view view{wire};
    auto begin = view.begin();
    auto result = parser.step(frame, begin, view.end());

    assert(result && *result);
    assert(frame.payload == "Hello");
    assert(frame.mask_key == key);

    std::cout << "[PASS] client_serializer_mask_bit_set_and_roundtrip\n";
}

static void test_server_close_frame_format() {
    const std::string wire = ServerFrameSerializer::close(CloseCode::NORMAL);
    assert((static_cast<uint8_t>(wire[0]) & 0x0Fu) == static_cast<uint8_t>(Opcode::CLOSE));
    assert((static_cast<uint8_t>(wire[0]) & 0x80u) != 0u);  // FIN set
    assert((static_cast<uint8_t>(wire[1]) & 0x7Fu) == 2u);  // 2 bytes: code only
    // Code 1000 = 0x03E8
    assert(static_cast<uint8_t>(wire[2]) == 0x03u);
    assert(static_cast<uint8_t>(wire[3]) == 0xE8u);

    std::cout << "[PASS] server_close_frame_format\n";
}

// ─── Handshake tests ─────────────────────────────────────────────────────────

static void test_accept_key_known_vector() {
    // RFC 6455 §1.3 example.
    const std::string accept = ws::makeAcceptKey("dGhlIHNhbXBsZSBub25jZQ==");
    assert(accept == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
    std::cout << "[PASS] accept_key_known_vector\n";
}

static void test_validateRequest_valid() {
    assert(ws::validateRequest("websocket", "Upgrade", "13"));
    assert(ws::validateRequest("WebSocket", "keep-alive, Upgrade", "13"));
    std::cout << "[PASS] validateRequest_valid\n";
}

static void test_validateRequest_invalid() {
    assert(!ws::validateRequest("http", "Upgrade", "13"));       // wrong upgrade
    assert(!ws::validateRequest("websocket", "keep-alive", "13")); // missing Upgrade in connection
    assert(!ws::validateRequest("websocket", "Upgrade", "8"));   // wrong version
    std::cout << "[PASS] validateRequest_invalid\n";
}

// ─── main ────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "=== WebSocket parser / serializer tests ===\n\n";

    // ClientFrameParser
    test_parse_short_text_frame();
    test_parse_empty_payload_frame();
    test_parse_two_frames_sequential();
    test_parse_incremental_byte_by_byte();
    test_rejects_unmasked_client_frame();
    test_rejects_reserved_bits();
    test_rejects_fragmented_control_frame();

    // ServerFrameParser
    test_server_parser_accepts_unmasked();
    test_server_parser_rejects_masked();

    // Serializers
    test_server_serializer_mask_bit_clear();
    test_client_serializer_mask_bit_set_and_roundtrip();
    test_server_close_frame_format();

    // Handshake
    test_accept_key_known_vector();
    test_validateRequest_valid();
    test_validateRequest_invalid();

    std::cout << "\nAll tests passed.\n";
    return 0;
}
