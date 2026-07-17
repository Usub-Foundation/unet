# Architecture

## The runtime pieces

- `usub::Uvent` - the coroutine scheduler & event loop.
- `usub::unet::http::ServerImpl<Router, Streams...>` - accept loop, protocol dispatch, & handler wiring.
- `usub::unet::http::router::Radix` - default router with middleware & error-handler slots.
- Streams: `PlainText`, `OpenSSLStream<ALPN...>`, `SChannelStream<ALPN...>`.
- Sessions: `v1::ServerSession<R>` for HTTP/1.1, `v2::ServerSession<R>` for HTTP/2. WebSocket runs its own `ws::Session<HandlerT>`.

`ServerRadix` & `ServerRegex` are convenience typedefs for the common configs.

## Server lifecycle

```cpp
usub::Uvent runtime{n_threads};
usub::unet::http::ServerRadix server{runtime, config};

server.handle("GET", "/path", handler);
server.addMiddleware(MIDDLEWARE_PHASE::HEADER, middleware);
server.onHTTP2Connection([](v2::Connection &c){ ... });

runtime.run();   // blocks; the runtime drives everything from here
```

There's no `server.run()`. The event loop belongs to `Uvent`, & the acceptors register themselves onto it in the server's constructor. When `runtime.run()` returns, all acceptors & sessions have wound down.

## What happens per connection

For each accepted socket, `ServerImpl::dispatchProtocol` decides:

1. TLS with ALPN? Use the negotiated protocol.
   - `h2` -> `v2::ServerSession`.
   - `http/1.1` -> `v1::ServerSession`.
2. No ALPN (plain socket)?
   - If h2c prior-knowledge is enabled & the first bytes match the h2 preface, promote to `v2::ServerSession`.
   - Otherwise `v1::ServerSession`.

Both sessions capture `PeerInfo` (ip, port, alpn, ssl) once at bootstrap & attach it to every `RequestReader`. Handlers read `req.peer.ip` without asking the transport.

## HTTP/1 path

`v1::ServerSession::loop`:

1. Slowloris guard: whole header phase must finish inside `keep_alive_deadline` on the first byte, then `idle_header_timeout` after.
2. `RequestParser::step(request, begin, end)` runs incrementally until state == `HEADERS_DONE`.
3. `negotiateKeepAlive` reads `Connection` & `Keep-Alive` headers, decides whether to reuse the socket.
4. Optional h2c upgrade: if `Upgrade: h2c` is present, hand the transport to the h2 upgrade handler & return.
5. Router match -> global HEADER middleware -> route HEADER middleware.
6. Handler spawned as a separate coroutine. The main loop keeps parsing the body & pushing chunks into `req.getBodyChannel()` for the handler to read.
7. On body EOF (`STATE::COMPLETE`), the body channel closes. Handler runs to completion. Response is serialized by `v1::ResponseSerializer`.
8. Loop back to step 1 if keep-alive is on.

## HTTP/2 path

`v2::ServerSession::loop`:

1. Read the preface (unless the connection came in via the h2c upgrade, in which case stream 1 is preseeded).
2. Send our initial `SETTINGS`. Peer must ACK inside `settings_ack_timeout`.
3. Frame loop reads whole frames via `tryDispatchOneFrame`, dispatches by type: DATA / HEADERS / CONTINUATION / SETTINGS / WINDOW_UPDATE / PING / GOAWAY / RST_STREAM / PRIORITY.
4. HEADERS + optional CONTINUATION accumulate into one HPACK block. When END_HEADERS is set, decode & dispatch the request (spawn `dispatchRequest`).
5. DATA frames flow into `stream.request.getBodyChannel()` while the handler runs.
6. HEADERS after HEADERS_DONE = trailers. Second HEADERS block MUST carry END_STREAM (RFC 9113 §8.1).
7. Handler writes a response via `bindStreamOps` (headers + DATA frames + optional trailers). All frames go through `pushOutbound` into a 2048-slot channel; a single `writerLoop` coroutine drains it & does the socket send.

### Stream retirement

Streams leave `streams_` via `retireStream` (closes the body channel & erases). Paths that call it: RST_STREAM in or out, close_stream on tunnels, content-length mismatch, STREAM_CLOSED errors. Normal request/response streams are erased in `finalizeStreamResponse` after the response completes.

## WebSocket path

`ws::Session<HandlerT>` reads frames via `FrameParser`, enforces:

- Every client frame MUST be masked (RFC 6455 §5.1).
- No overlapping fragmented messages (RFC 6455 §5.4).
- Control frames may interleave inside a fragmented data message.

Data frames spawn the handler method once per message; payload bytes stream into a per-message `ClientReaderChannel` the handler pops.

Runs on top of either a `TCP<Stream, Socket>` transport (h1 upgrade) or a `v2::StreamTransport` (extended CONNECT over h2).

## Router & middleware placement

- Global middleware: `Radix::middleware_chain_`, phase-keyed. HEADER phase runs before match, BODY phase currently runs from the h1 body-read loop.
- Route middleware: attached to the matched `RadixRoute`, run after the route matches but before the handler.
- Error handlers: string-keyed via `addErrorHandler("<code>", fn)`. The runtime dispatches by the response status code as a string (`"404"`, `"405"`, `"500"`, ...). Unhandled codes fall through to `defaultErrorResponse` which emits a minimal plain-text body so nothing hangs.

## Acceptors, streams, & config

`ServerImpl` holds one `Acceptor<Stream>` per template stream. Each acceptor reads its own config section:

- `Acceptor<PlainText>` reads `HTTP.PlainTextStream`.
- `Acceptor<OpenSSLStream<...>>` reads `HTTP.OpenSSLStream`.
- `Acceptor<SChannelStream<...>>` reads `HTTP.SChannelStream`.

Missing sections fall back to built-in defaults. Full key list: [Configuration](config.md).

## The h2 outbound channel

All h2 frame writes go through one point:

```cpp
pushOutbound(std::string bytes);   // try_send into outbound_channel_ (2048 slots)
```

Non-blocking. If the channel is full, we flip `goaway_pending_` & close the channel - the peer either isn't reading or the socket is congested past what the buffer absorbs, & we bail. One `writerLoop` coroutine drains the channel & is the only place `transport->send` is called; this is what preserves HEADERS + CONTINUATION atomicity on the wire.

## Body streaming under upgrade

For h2 tunnels (VLESS, WebSocket-over-h2), the same `BodyReaderChannel` the handler could read from is wrapped by `StreamTransport`. Bytes the handler didn't drain before calling `ctx.accept` are still there for the spawned protocol. One queue, two possible readers, no drain-and-carry.
