# Upgrade routes

An upgrade route hands the transport off to a different protocol once the request has been examined & accepted. This is how WebSocket, VLESS, & custom tunneled protocols run inside `unet`. Same mechanism, one protocol per registered route.

Two things happen on a successful upgrade:

1. The framework serializes the response you built up (typically a 101 or a 200 with special headers) & flushes it.
2. The framework calls `ctx.spawn(transport, carry_buffer)` with a moved-out transport. Your spawned coroutine now owns the wire.

## Registering an upgrade route

Same shape as `handle`, but the handler takes a third `UpgradeContext &` argument, & you register with `handleUpgrade`:

```cpp
usub::uvent::task::Awaitable<void>
my_upgrade(usub::unet::http::RequestReader &req,
           usub::unet::http::ResponseWriter &res,
           usub::unet::http::UpgradeContext &ctx) {
    // Inspect req, decide whether to accept.
    if (!looks_ok(req)) {
        res.metadata.status_code = 400;
        co_await res.send(std::string{"nope\n"});
        co_return;  // ctx.accepted stays false, no upgrade happens
    }

    // Set up the response headers your protocol wants clients to see.
    res.metadata.status_code = 101;   // or 200 for h2 tunnels
    res.headers.addHeader("upgrade", "my-protocol");
    res.headers.addHeader("connection", "upgrade");

    // Register the coroutine that takes over the wire.
    ctx.accept([](std::unique_ptr<usub::unet::core::transport::Transport> tr,
                  usub::unet::Buffer carry) -> usub::uvent::task::Awaitable<void> {
        // tr is now yours. carry may have bytes that came in after the
        // request headers but before we handed off.
        for (;;) {
            usub::unet::Buffer buf;
            const ssize_t n = co_await tr->read(buf);
            if (n <= 0) co_return;
            // ... your protocol logic
        }
    });
    co_return;
}

server.handleUpgrade("GET", "/my-proto", my_upgrade);
```

## `UpgradeContext`

Defined in `unet/http/upgrade_context.hpp`:

```cpp
struct UpgradeContext {
    bool accepted{false};
    upgrade_action<Awaitable<void>(std::unique_ptr<Transport>, Buffer)> spawn;

    void accept(upgrade_action<...> action);   // sets accepted=true & moves action into spawn
};
```

If `ctx.accepted` is `false` when your handler returns, the framework treats the response as a normal request/response cycle - the response goes out, connection stays or closes per HTTP semantics, no upgrade.

If `ctx.accepted` is `true`, the framework flushes the response, releases the transport ownership, & invokes `ctx.spawn(transport, carry)` on a new coroutine.

## The `carry` buffer

Right after headers, the parser may have read further into the socket than the request required. Those extra bytes are the `carry` argument. Your spawned protocol usually treats it as "what came in before I took over" & processes it before reading more from the transport.

Empty carry is common. Non-empty carry happens when the client pipelined data after the headers.

## Under HTTP/1

The transport handed to `spawn` is the raw `TCP<Stream, Socket>` - the same socket the request came in on. Reading & writing goes straight to the wire.

## Under HTTP/2

The transport handed to `spawn` is a `v2::StreamTransport`. Reads pop from the h2 stream's `BodyReaderChannel` (DATA frames feed it). Writes go through `enqueueDataFrames` & then the h2 outbound channel. Every read chunk is one or more DATA frames' worth of bytes; every write becomes DATA frames (chunked by `max_frame_size`).

`shutdown()` on a `StreamTransport` sends `RST_STREAM(CANCEL)` & retires the stream from `streams_`. If you `co_return` without calling `shutdown`, the stream lingers until the connection tears down. Not a leak per se (bounded by `MAX_CONCURRENT_STREAMS`), but it's clutter.

## Registered examples

### VLESS

The [`examples/VLESS.cpp`](../examples/VLESS.cpp) upgrade handler is a single call:

```cpp
server.handleUpgrade("POST", "/vless",
    [auth](usub::unet::http::RequestReader &req,
           usub::unet::http::ResponseWriter &res,
           usub::unet::http::UpgradeContext &ctx)
       -> usub::uvent::task::Awaitable<void> {
        co_await usub::unet::vless::upgrade(req, res, ctx, auth);
    });
```

`vless::upgrade` reads the VLESS header off the request body, authenticates the UUID against the provider, & wires the destination-facing socket to both directions of the tunnel.

### WebSocket

```cpp
server.handleUpgrade(std::set<std::string>{"GET", "CONNECT"}, "/ws",
                     usub::unet::ws::upgradeHandler(chat_handler));
```

`ws::upgradeHandler` is a factory that produces the `ctx.accept(...)` closure for you. See [WebSockets](websockets.md).

## Notes

- Middleware runs before the upgrade handler. Same HEADER-phase chain as normal handlers.
- Read the request body inside the handler if you need it before deciding, using `req.chunk()` / `req.collect(limit)`. Bytes the handler didn't drain remain in the body channel & are visible to the spawned protocol through `StreamTransport` (h2) or the shared `carry` buffer (h1).
- `ctx.spawn` runs detached. Capture what it needs by value or via `shared_ptr`. The `RequestReader`/`ResponseWriter` references are gone by the time it fires.
