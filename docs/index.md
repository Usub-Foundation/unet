# unet Documentation

C++23 coroutine-based HTTP / HTTP2 / WebSocket library on top of [uvent](https://github.com/Usub-development/uvent). This doc set describes what the code actually does today, not what a marketing page would say it does.

## Where to start

New here?

- [Installation](installation.md) - build & consume from CMake.
- [Quick Start](quick-start.md) - the smallest working server, in five minutes.
- [Architecture](architecture.md) - how connections turn into handler calls.

Building something?

- [Routing](routing.md) - radix patterns, params, constraints.
- [Handlers](handler.md) - handler signatures, streaming, sendfile.
- [Middlewares](middlewares.md) - phases, order, per-route vs global.
- [Request & Response](request-response.md) - the reader/writer types your handlers touch.
- [Configuration](config.md) - `Config` object shape & recognized keys.

Beyond plain HTTP?

- [WebSockets](websockets.md) - RFC 6455 over h1, RFC 8441 over h2.
- [Upgrade routes](upgrade.md) - hand the transport off to your own protocol (VLESS example).
- [HTTP Client](client.md) - outbound requests, keep-alive, TLS, proxies.

Reference:

- [Parsing](parsing.md) - h1 wire parsers, error surface.
- [Multipart Form Data](multipart-form-data.md) - `multipart/form-data` decoder.
- [Custom Routers](custom-router.md) - swap `Radix` for your own type.

Project process:

- [Roadmap](roadmap.md) - what's next, what's out of scope.
- [Contributing](contributing.md) - workflow & style.

## What's real vs aspirational

Working & tested:

- HTTP/1.1 server (RFC 9112), including all four request-target forms & chunked transfer.
- HTTP/2 server (RFC 9113) with HPACK, per-stream flow control, extensions.
- WebSocket server (RFC 6455) with fragmented messages, ping/pong, close frames.
- WebSocket-over-HTTP/2 via extended CONNECT (RFC 8441).
- Radix router.
- Middleware chain (HEADER & BODY phases invoke).

Working but partial:

- HTTP/1 client. Keep-alive, TLS, proxies, `CONNECT` tunneling. HTTP/2 client is not implemented.
- h2c upgrade (RFC 7540 §3.2). Wired, may get retired since RFC 9113 deprecates it.
- h2c prior-knowledge sniff. Optional, off by default.

Not yet:

- HTTP/2 client.
- WebSocket `permessage-deflate` (RFC 7692).
- HTTP/3.
- Response-phase middleware invocation in the h1 send path.
- Body-trickle timeout (only header-trickle is guarded today).

## API stability

Names in these docs match `include/unet/**`. When the header changes, these files should change with it. If you spot drift, open an issue or PR - see [Contributing](contributing.md).
