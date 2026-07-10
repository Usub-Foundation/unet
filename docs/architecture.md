# Architecture

## Runtime Pieces

The current HTTP runtime is composed of:

- `usub::Uvent`: event loop and coroutine scheduler
- `usub::unet::http::ServerImpl<Router, Streams...>`: acceptor bootstrap and connection loop
- `usub::unet::http::router::Radix`: route matching, middleware containers, and error handlers
- stream types such as `PlainText` and `OpenSSLStream<>`
- HTTP session implementations for `HTTP/1.1` (`v1::ServerSession<R>`) and `HTTP/2.0` (`v2::ServerSession<R>`)

## Server Lifecycle

Typical startup sequence:

1. Construct `usub::Uvent`.
2. Construct a server (`ServerRadix` or custom `ServerImpl<...>`).
3. Register routes, middleware, and error handlers.
4. Run `uvent` with `runtime.run()`.

There is no `server.run()` API in the current `unet` server implementation.

## Connection Pipeline

For each accepted socket:

1. `Bootstrap` receives initial bytes and decides which protocol the client speaks.
   - HTTP/2 prior-knowledge preface → `v2::ServerSession<RouterType>`.
   - Anything else → `v1::ServerSession<RouterType>`. From there a client may still
     ask for h2c via `Upgrade: h2c`, which swaps the session to `v2`.
2. The chosen session takes over and runs version-specific logic:
   - **HTTP/1**: request parser consumes bytes incrementally; on `STEP::HEADERS`
     the router matches, then global/route header middleware run; body parsing
     and middleware follow; handler executes on `STEP::COMPLETE`; response is
     serialized via `v1::ResponseSerializer`.
   - **HTTP/2**: a frame loop reads the preface (or accepts the upgrade-seeded
     stream), exchanges SETTINGS, dispatches builtin frame handlers (DATA,
     HEADERS, SETTINGS, WINDOW_UPDATE, PING, GOAWAY, RST_STREAM, CONTINUATION,
     PRIORITY). Stream completion (END_STREAM) queues for dispatch. The router
     match + handler invoke happen once per stream; multiple streams multiplex
     on one connection.
3. Both sessions reset per-request state to keep the connection alive when
   permitted; both expose `connection()` returning a user-facing handle
   (`v1::Connection&` or `v2::Connection`) delivered to the `on_http1_connection`
   / `on_http2_connection` hooks held on the `MiddlewareChain`.

## Router and Middleware Placement

- Global middleware: `Radix::middleware_chain_`
- Route middleware: `RadixRoute::middleware_chain`
- Error handlers: string-keyed (`addErrorHandler(level, fn)`)

## Streams and Acceptors

`ServerImpl` owns an `Acceptor<T>` for each stream in template parameters.

- `Acceptor<PlainText>` reads `HTTP.PlainTextStream` config section
- `Acceptor<OpenSSLStream<...>>` reads `HTTP.OpenSSLStream` config section

If config is empty, each acceptor falls back to built-in defaults.
