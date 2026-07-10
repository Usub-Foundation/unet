# unet Documentation

> **Disclaimer:** Due to life constraints, I could not write this documentation myself. This documentation was generated from the current repository state. We will review this documentation later.

`unet` is a coroutine-based C++ networking library focused on HTTP request/response handling, routing, middleware, parsing, and client/server primitives on top of `uvent`.

## Scope Of This Documentation

This documentation focuses on the parts that are currently practical to use:

- HTTP/1 server flow (`ServerImpl` / `ServerRadix`, `v1::ServerSession<R>`)
- HTTP/2 server flow (`v2::ServerSession<R>`) — prior-knowledge preface and
  `Upgrade: h2c` are both wired; frame layer, HPACK, and SETTINGS exchange are
  in place; per-stream tunneling (CONNECT, Extended CONNECT, gRPC trailers) is
  not yet implemented.
- HTTP/1 wire parser and serializer
- HTTP client (`ClientImpl`) with plain and TLS streams (HTTP/1 only on the
  client side for now)
- Byte-level radix router with named params, inline regex constraints, and
  named wildcards
- Middleware, headers, and multipart helpers

Out of scope in this docs set:

- mail/IMAP model headers (not part of active HTTP runtime)
- old `examples/` behavior (mostly legacy)

## Quick Links

- Getting started: [Installation](installation.md), [Quick Start](quick-start.md)
- Runtime setup: [Architecture](architecture.md), [Configuration](config.md)
- Request handling: [Routing](routing.md), [Writing Custom Routers](custom-router.md), [Middlewares](middlewares.md), [Request and Response](request-response.md), [Handlers](handler.md)
- Outbound requests: [HTTP Client](client.md)
- Parsing internals: [Parsing](parsing.md), [Experimental Parse Error Notes](Experimental/parse_error_handling.md)
- Utilities: [Multipart Form Data](multipart-form-data.md)
- Project process: [Roadmap](roadmap.md), [Contributing](contributing.md)

## Current Reality Snapshot

- HTTP/1 parser, core flow, router, and middleware chain are the most mature paths.
- HTTP/2 server can speak prior-knowledge and h2c-upgrade traffic end-to-end.
  CONNECT/Extended-CONNECT and gRPC trailers are not implemented yet.
- HTTP/2 client is not implemented.
- Middleware phases exist, but runtime invocation details matter (see [Middlewares](middlewares.md)).
- Tests are present and useful for behavior clues, but they are not exhaustive quality gates yet.
- API names in these docs match current headers under `include/unet/**`.
