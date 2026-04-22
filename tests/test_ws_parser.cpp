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
static std::expected<bool, usub::unet::ws::FrameParseError>
feedClient(usub::unet::ws::ClientFrameParser &parser, usub::unet::ws::ClientFrame &frame, const std::string &bytes) {
    std::string_view view{bytes};
    auto begin = view.begin();
    return parser.step(frame, begin, view.end());
}

// Build a minimal masked client frame manually (TEXT, single-byte mask key 0x00).
static std::string makeMaskedFrame(uint8_t opcode, std::string_view payload,
                                     std::array<uint8_t,4> mask = {0x37,0xfa,0x21,0x3d}) {
    usub::unet::ws::ClientFrame f;
    f.fin      = true;
    f.opcode   = opcode;
    f.mask_key = mask;
    f.payload  = std::string(payload);
    return usub::unet::ws::ClientFrameSerializer::serialize(f);
}

// Build an unmasked server frame manually.
static std::string makeServerFrame(uint8_t opcode, std::string_view payload) {
    usub::unet::ws::ServerFrame f;
    f.fin     = true;
    f.opcode  = opcode;
    f.payload = std::string(payload);
    return usub::unet::ws::ServerFrameSerializer::serialize(f);
}

// ─── ClientFrameParser tests ─────────────────────────────────────────────────

static void testParseShortTextFrame() {
    const std::array<uint8_t,4> key = {0x37, 0xfa, 0x21, 0x3d};
    const std::string plain = "Hello";

    const std::string wire = makeMaskedFrame(static_cast<uint8_t>(usub::unet::ws::Opcode::TEXT), plain, key);

    usub::unet::ws::ClientFrameParser parser;
    usub::unet::ws::ClientFrame frame;
    auto result = feedClient(parser, frame, wire);

    assert(result && *result && "frame should be complete");
    assert(frame.fin);
    assert(frame.opcode == static_cast<uint8_t>(usub::unet::ws::Opcode::TEXT));
    assert(frame.mask_key == key);
    assert(frame.payload == plain);

    std::cout << "[PASS] parseShortTextFrame\n";
}

static void testParseEmptyPayloadFrame() {
    const std::string wire = makeMaskedFrame(static_cast<uint8_t>(usub::unet::ws::Opcode::PING), "");

    usub::unet::ws::ClientFrameParser parser;
    usub::unet::ws::ClientFrame frame;
    auto result = feedClient(parser, frame, wire);

    assert(result && *result);
    assert(frame.opcode == static_cast<uint8_t>(usub::unet::ws::Opcode::PING));
    assert(frame.payload.empty());

    std::cout << "[PASS] parseEmptyPayloadFrame\n";
}

static void testParseTwoFramesSequential() {
    const std::array<uint8_t,4> key = {0x01, 0x02, 0x03, 0x04};
    const std::string wire =
        makeMaskedFrame(static_cast<uint8_t>(usub::unet::ws::Opcode::TEXT), "foo", key) +
        makeMaskedFrame(static_cast<uint8_t>(usub::unet::ws::Opcode::BINARY), "bar", key);

    std::string_view view{wire};
    auto begin = view.begin();
    const auto end = view.end();

    usub::unet::ws::ClientFrameParser parser;
    usub::unet::ws::ClientFrame frame;

    auto r1 = parser.step(frame, begin, end);
    assert(r1 && *r1);
    assert(frame.payload == "foo");
    parser.reset();

    frame = {};
    auto r2 = parser.step(frame, begin, end);
    assert(r2 && *r2);
    assert(frame.payload == "bar");
    assert(frame.opcode == static_cast<uint8_t>(usub::unet::ws::Opcode::BINARY));

    std::cout << "[PASS] parseTwoFramesSequential\n";
}

static void testParseIncrementalByteByByte() {
    const std::string wire = makeMaskedFrame(static_cast<uint8_t>(usub::unet::ws::Opcode::TEXT), "Hi");

    usub::unet::ws::ClientFrameParser parser;
    usub::unet::ws::ClientFrame frame;
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

    std::cout << "[PASS] parseIncrementalByteByByte\n";
}

static void testRejectsUnmaskedClientFrame() {
    // Build a valid TEXT frame bytes, then clear the MASK bit (byte 1).
    const std::string wire = makeMaskedFrame(static_cast<uint8_t>(usub::unet::ws::Opcode::TEXT), "secret");
    std::string bad = wire;
    bad[1] &= ~0x80u;  // clear MASK bit

    usub::unet::ws::ClientFrameParser parser;
    usub::unet::ws::ClientFrame frame;
    auto result = feedClient(parser, frame, bad);

    assert(!result && "should return error");
    assert(result.error().code == usub::unet::ws::FrameParseError::CODE::UNMASKED_CLIENT_FRAME);
    assert(parser.getContext().state == usub::unet::ws::ClientFrameParser::STATE::FAILED);

    std::cout << "[PASS] rejectsUnmaskedClientFrame\n";
}

static void testRejectsReservedBits() {
    std::string wire = makeMaskedFrame(static_cast<uint8_t>(usub::unet::ws::Opcode::TEXT), "x");
    wire[0] |= 0x40u;  // set RSV1

    usub::unet::ws::ClientFrameParser parser;
    usub::unet::ws::ClientFrame frame;
    auto result = feedClient(parser, frame, wire);

    assert(!result);
    assert(result.error().code == usub::unet::ws::FrameParseError::CODE::RESERVED_BITS_SET);

    std::cout << "[PASS] rejectsReservedBits\n";
}

static void testRejectsFragmentedControlFrame() {
    std::string wire = makeMaskedFrame(static_cast<uint8_t>(usub::unet::ws::Opcode::PING), "");
    wire[0] &= ~0x80u;  // clear FIN bit → fragmented control

    usub::unet::ws::ClientFrameParser parser;
    usub::unet::ws::ClientFrame frame;
    auto result = feedClient(parser, frame, wire);

    assert(!result);
    assert(result.error().code == usub::unet::ws::FrameParseError::CODE::CONTROL_FRAME_FRAGMENTED);

    std::cout << "[PASS] rejectsFragmentedControlFrame\n";
}

// ─── ServerFrameParser tests ─────────────────────────────────────────────────

static void testServerParserAcceptsUnmasked() {
    const std::string wire = makeServerFrame(static_cast<uint8_t>(usub::unet::ws::Opcode::TEXT), "world");

    usub::unet::ws::ServerFrameParser parser;
    usub::unet::ws::ServerFrame frame;
    std::string_view view{wire};
    auto begin = view.begin();
    auto result = parser.step(frame, begin, view.end());

    assert(result && *result);
    assert(frame.payload == "world");
    assert(frame.opcode == static_cast<uint8_t>(usub::unet::ws::Opcode::TEXT));

    std::cout << "[PASS] serverParserAcceptsUnmasked\n";
}

static void testServerParserRejectsMasked() {
    // Build a masked frame and present it to ServerFrameParser.
    const std::string wire = makeMaskedFrame(static_cast<uint8_t>(usub::unet::ws::Opcode::TEXT), "hi");

    usub::unet::ws::ServerFrameParser parser;
    usub::unet::ws::ServerFrame frame;
    std::string_view view{wire};
    auto begin = view.begin();
    auto result = parser.step(frame, begin, view.end());

    assert(!result);
    assert(result.error().code == usub::unet::ws::FrameParseError::CODE::MASKED_SERVER_FRAME);

    std::cout << "[PASS] serverParserRejectsMasked\n";
}

// ─── Serializer tests ────────────────────────────────────────────────────────

static void testServerSerializerMaskBitClear() {
    const std::string wire = usub::unet::ws::ServerFrameSerializer::text("hello");
    // Byte 1 must not have the MASK bit set (0x80).
    assert((static_cast<uint8_t>(wire[1]) & 0x80u) == 0u);
    assert((static_cast<uint8_t>(wire[1]) & 0x7Fu) == 5u);  // payload length 5

    std::cout << "[PASS] serverSerializerMaskBitClear\n";
}

static void testClientSerializerMaskBitSetAndRoundtrip() {
    const std::array<uint8_t,4> key = {0xAB, 0xCD, 0xEF, 0x01};
    const std::string wire = usub::unet::ws::ClientFrameSerializer::text("Hello", key);

    // Byte 1: MASK bit set, length = 5.
    assert((static_cast<uint8_t>(wire[1]) & 0x80u) != 0u);
    assert((static_cast<uint8_t>(wire[1]) & 0x7Fu) == 5u);

    // Bytes 2–5: masking key.
    assert(static_cast<uint8_t>(wire[2]) == key[0]);
    assert(static_cast<uint8_t>(wire[3]) == key[1]);
    assert(static_cast<uint8_t>(wire[4]) == key[2]);
    assert(static_cast<uint8_t>(wire[5]) == key[3]);

    // Feed back through the parser — should recover "Hello".
    usub::unet::ws::ClientFrameParser parser;
    usub::unet::ws::ClientFrame frame;
    std::string_view view{wire};
    auto begin = view.begin();
    auto result = parser.step(frame, begin, view.end());

    assert(result && *result);
    assert(frame.payload == "Hello");
    assert(frame.mask_key == key);

    std::cout << "[PASS] clientSerializerMaskBitSetAndRoundtrip\n";
}

static void testServerCloseFrameFormat() {
    const std::string wire = usub::unet::ws::ServerFrameSerializer::close(usub::unet::ws::CloseCode::NORMAL);
    assert((static_cast<uint8_t>(wire[0]) & 0x0Fu) == static_cast<uint8_t>(usub::unet::ws::Opcode::CLOSE));
    assert((static_cast<uint8_t>(wire[0]) & 0x80u) != 0u);  // FIN set
    assert((static_cast<uint8_t>(wire[1]) & 0x7Fu) == 2u);  // 2 bytes: code only
    // Code 1000 = 0x03E8
    assert(static_cast<uint8_t>(wire[2]) == 0x03u);
    assert(static_cast<uint8_t>(wire[3]) == 0xE8u);

    std::cout << "[PASS] serverCloseFrameFormat\n";
}

// ─── Handshake tests ─────────────────────────────────────────────────────────

static void testAcceptKeyKnownVector() {
    // RFC 6455 §1.3 example.
    const std::string accept = ws::makeAcceptKey("dGhlIHNhbXBsZSBub25jZQ==");
    assert(accept == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
    std::cout << "[PASS] acceptKeyKnownVector\n";
}

static void testValidateRequestValid() {
    assert(ws::validateRequest("websocket", "Upgrade", "13"));
    assert(ws::validateRequest("WebSocket", "keep-alive, Upgrade", "13"));
    std::cout << "[PASS] validateRequestValid\n";
}

static void testValidateRequestInvalid() {
    assert(!ws::validateRequest("http", "Upgrade", "13"));       // wrong upgrade
    assert(!ws::validateRequest("websocket", "keep-alive", "13")); // missing Upgrade in connection
    assert(!ws::validateRequest("websocket", "Upgrade", "8"));   // wrong version
    std::cout << "[PASS] validateRequestInvalid\n";
}

// ─── main ────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "=== WebSocket parser / serializer tests ===\n\n";

    // ClientFrameParser
    testParseShortTextFrame();
    testParseEmptyPayloadFrame();
    testParseTwoFramesSequential();
    testParseIncrementalByteByByte();
    testRejectsUnmaskedClientFrame();
    testRejectsReservedBits();
    testRejectsFragmentedControlFrame();

    // ServerFrameParser
    testServerParserAcceptsUnmasked();
    testServerParserRejectsMasked();

    // Serializers
    testServerSerializerMaskBitClear();
    testClientSerializerMaskBitSetAndRoundtrip();
    testServerCloseFrameFormat();

    // Handshake
    testAcceptKeyKnownVector();
    testValidateRequestValid();
    testValidateRequestInvalid();

    std::cout << "\nAll tests passed.\n";
    return 0;
}
