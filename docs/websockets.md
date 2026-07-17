# WebSockets

WebSocket support covers RFC 6455 (the classic h1 `Upgrade: websocket` dance) & RFC 8441 (extended CONNECT over HTTP/2). Both run through the same handler class you write once - the framework picks the wire path from what the client asked for.

Not yet: `permessage-deflate` (RFC 7692). The parser rejects any non-zero RSV bit today.

## Writing a handler

Derive from `usub::unet::ws::Handler` & override the methods you care about. Everything else no-ops.

```cpp
#include <unet/ws/handler.hpp>
#include <unet/ws/reader.hpp>
#include <unet/ws/writer.hpp>

class ChatHandler : public usub::unet::ws::Handler {
public:
    usub::uvent::task::Awaitable<void>
    onOpen(usub::unet::ws::ServerWriter writer) override {
        std::scoped_lock lk(this->mu_);
        this->peers_.push_back(writer);
        co_return;
    }

    usub::uvent::task::Awaitable<void>
    text(usub::unet::ws::ClientReader &reader,
         usub::unet::ws::ServerWriter writer) override {
        auto msg = co_await reader.accumulatePayload();
        if (!msg) co_return;

        std::string relay = "user " + std::to_string(writer.id()) + ": " + *msg;
        for (auto &p : this->snapshot()) co_await p.sendText(relay);
    }

    usub::uvent::task::Awaitable<void>
    onClose(usub::unet::ws::ServerWriter writer) override {
        std::scoped_lock lk(this->mu_);
        std::erase_if(this->peers_,
                      [id = writer.id()](auto &w){ return w.id() == id; });
        co_return;
    }

private:
    std::vector<usub::unet::ws::ServerWriter> snapshot() {
        std::scoped_lock lk(this->mu_);
        return this->peers_;
    }
    std::mutex mu_;
    std::vector<usub::unet::ws::ServerWriter> peers_;
};
```

## Registering the upgrade

`ws::upgradeHandler` wraps a `std::shared_ptr<HandlerT>` into an upgrade route the router understands:

```cpp
auto chat = std::make_shared<ChatHandler>();

server.handleUpgrade(std::set<std::string>{"GET", "CONNECT"}, "/ws",
                     usub::unet::ws::upgradeHandler(chat));
```

`GET` accepts the RFC 6455 `Upgrade: websocket` request from an HTTP/1.1 client. `CONNECT` accepts the RFC 8441 extended CONNECT from an HTTP/2 client (`:method=CONNECT`, `:protocol=websocket`).

The same server can serve both. The client picks the wire; your handler doesn't care.

## Handler method reference

Data frames - each spawns your method once per message:

- `text(ClientReader &, ServerWriter)` - TEXT opcode.
- `binary(ClientReader &, ServerWriter)` - BINARY opcode.
- `reserved3..7(ClientReader &, ServerWriter)` - reserved data opcodes; framework passes them through.

Control frames - fire per-frame with the whole payload materialized:

- `ping(ServerWriter, const Frame &)`
- `pong(ServerWriter, const Frame &)`
- `close(ServerWriter, const Frame &)` - after this fires, the socket shuts down.
- `reservedB..F(ServerWriter, const Frame &)`

Lifecycle:

- `onOpen(ServerWriter)` - right after the upgrade completes.
- `onClose(ServerWriter)` - right before the socket goes away, whether from a CLOSE frame or a transport error.

Handler-wide config:

- `timeout_ms` (default `20000`) - idle read timeout on the socket. Rearms on every successful read.

## `ServerWriter`

Value type, cheap to copy, holds a `shared_ptr<WriteState>`. Safe to stash in your own map so you can broadcast from outside the handler callbacks.

```cpp
co_await writer.sendText("hi");
co_await writer.sendBinary({raw_bytes});
co_await writer.sendFrame(frame);       // arbitrary opcode
co_await writer.ping();                 // empty payload
co_await writer.pong();
co_await writer.close(usub::unet::ws::CLOSE_CODE::NORMAL, "bye");

writer.expired();   // transport gone?
writer.id();        // monotonic connection id, useful as a peer identifier
```

All send methods serialize behind a per-connection mutex-guarded queue - only one drain coroutine runs at a time. Frame order is preserved even when two coroutines call at once.

## `ClientReader`

For data frames. `accumulatePayload()` reads the whole message (all fragments) & returns `std::optional<std::string>`. Empty optional means the peer closed mid-message or the frame was invalid.

Streaming access to individual fragments is possible via the underlying channel, but the accumulator is what you want 95% of the time.

## Fragmentation & control frames

The session enforces RFC 6455 §5.4:

- No new TEXT/BINARY while a fragmented message is in flight.
- No CONTINUATION when nothing is open.
- Control frames may interleave inside a fragmented data message.
- Every client frame MUST be masked (RFC 6455 §5.1). Unmasked frames are a protocol error & we close with `1002`.

## Pings

There's no built-in ping loop - opinion left to the handler:

```cpp
static usub::uvent::task::Awaitable<void>
pingLoop(usub::unet::ws::ServerWriter writer) {
    while (!writer.expired()) {
        co_await usub::uvent::system::this_coroutine::sleep_for(std::chrono::seconds{15});
        if (writer.expired()) co_return;
        co_await writer.ping();
    }
}

// From onOpen:
usub::uvent::system::co_spawn(pingLoop(writer));
```

## WebSocket over h2

When the client uses RFC 8441 extended CONNECT, the h2 stream becomes the WebSocket transport. Reads pull from the same body channel HTTP/2 DATA frames feed. Writes go through `StreamTransport::send` which chunks into DATA frames. All of it transparent to your handler.

Note: `permessage-deflate` isn't wired. The h2 flow-control window applies to DATA frames, so if the peer isn't opening the window, `sendText` will suspend on `send_window_wake_`.
