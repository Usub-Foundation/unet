# unet Documentation

> **Disclaimer:** Due to life constraints, I could not write this documentation myself. This documentation was generated from the current repository state. We will review this documentation later.

`unet` is a coroutine-based C++ networking library focused on HTTP/1 request/response handling, routing, middleware, parsing, and client/server primitives on top of `uvent`.

## Scope Of This Documentation

This documentation intentionally focuses on the parts that are currently practical to use:

- HTTP/1 server flow (`ServerImpl` / `ServerRadix`)
- HTTP/1 wire parser and serializer
- HTTP client (`ClientImpl`) with plain and TLS streams
- Router, middleware, headers, and multipart helpers

Out of scope in this docs set:

- HTTP/2 internals (still in progress)
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

- HTTP/1 parser and core flow are the primary implemented path.
- Middleware phases exist, but runtime invocation details matter (see [Middlewares](middlewares.md)).
- Tests are present and useful for behavior clues, but they are not exhaustive quality gates yet.
- API names in this docs match current headers under `include/unet/**`.
