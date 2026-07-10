#include "unet/ws/handler.hpp"

namespace usub::unet::ws {

    namespace {
        Awaitable<void> drainAndDiscard(ClientReader &reader) {
            for (;;) {
                auto h = co_await reader.getFrameHeader();
                if (!h) co_return;
                (void) co_await reader.getPayload();
                if (h->fin) co_return;
            }
        }
    }// namespace

    Awaitable<void> Handler::text(ClientReader &reader, ServerWriter) { co_await drainAndDiscard(reader); }
    Awaitable<void> Handler::binary(ClientReader &reader, ServerWriter) { co_await drainAndDiscard(reader); }

    Awaitable<void> Handler::reserved3(ClientReader &, ServerWriter writer) {
        co_await writer.close(CLOSE_CODE::PROTOCOL_ERROR, "reserved opcode");
    }
    Awaitable<void> Handler::reserved4(ClientReader &, ServerWriter writer) {
        co_await writer.close(CLOSE_CODE::PROTOCOL_ERROR, "reserved opcode");
    }
    Awaitable<void> Handler::reserved5(ClientReader &, ServerWriter writer) {
        co_await writer.close(CLOSE_CODE::PROTOCOL_ERROR, "reserved opcode");
    }
    Awaitable<void> Handler::reserved6(ClientReader &, ServerWriter writer) {
        co_await writer.close(CLOSE_CODE::PROTOCOL_ERROR, "reserved opcode");
    }
    Awaitable<void> Handler::reserved7(ClientReader &, ServerWriter writer) {
        co_await writer.close(CLOSE_CODE::PROTOCOL_ERROR, "reserved opcode");
    }

    Awaitable<void> Handler::ping(ServerWriter writer, const Frame &frame) {
        co_await writer.pong(frame.payload);
        co_return;
    }
    Awaitable<void> Handler::pong(ServerWriter, const Frame &) { co_return; }
    Awaitable<void> Handler::close(ServerWriter writer, const Frame &) {
        co_await writer.close(CLOSE_CODE::NORMAL);
        co_return;
    }
    Awaitable<void> Handler::reservedB(ServerWriter writer, const Frame &) {
        co_await writer.close(CLOSE_CODE::PROTOCOL_ERROR, "reserved opcode");
    }
    Awaitable<void> Handler::reservedC(ServerWriter writer, const Frame &) {
        co_await writer.close(CLOSE_CODE::PROTOCOL_ERROR, "reserved opcode");
    }
    Awaitable<void> Handler::reservedD(ServerWriter writer, const Frame &) {
        co_await writer.close(CLOSE_CODE::PROTOCOL_ERROR, "reserved opcode");
    }
    Awaitable<void> Handler::reservedE(ServerWriter writer, const Frame &) {
        co_await writer.close(CLOSE_CODE::PROTOCOL_ERROR, "reserved opcode");
    }
    Awaitable<void> Handler::reservedF(ServerWriter writer, const Frame &) {
        co_await writer.close(CLOSE_CODE::PROTOCOL_ERROR, "reserved opcode");
    }

    Awaitable<void> Handler::onOpen(ServerWriter) { co_return; }
    Awaitable<void> Handler::onClose(ServerWriter) { co_return; }

}// namespace usub::unet::ws
