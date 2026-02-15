# Architecture

## Runtime Pieces

The current HTTP runtime is composed of:

- `usub::Uvent`: event loop and coroutine scheduler
- `usub::unet::http::ServerImpl<Router, Streams...>`: acceptor bootstrap and connection loop
- `usub::unet::http::router::Radix`: route matching, middleware containers, and error handlers
- stream types such as `PlainText` and `OpenSSLStream`
- HTTP session implementation for `HTTP/1.1`

## Server Lifecycle

Typical startup sequence:

1. Construct `usub::Uvent`.
2. Construct a server (`ServerRadix` or custom `ServerImpl<...>`).
3. Register routes, middleware, and error handlers.
4. Run `uvent` with `runtime.run()`.

There is no `server.run()` API in the current `unet` server implementation.

## Connection Pipeline (HTTP/1)

For each accepted socket:

1. `Bootstrap` receives initial bytes and decides protocol target.
2. For HTTP/1 traffic, session upgrades to `ServerSession<VERSION::HTTP_1_1, RouterType>`.
3. Request parser consumes bytes incrementally.
4. When headers are complete:
   - route match is attempted
   - global header middleware runs
   - route header middleware runs
5. Body parsing continues (content-length or chunked), body middleware may run.
6. Route handler coroutine executes when request is complete.
7. Response is serialized (`v1::ResponseSerializer`) and written.
8. Request/response state is reset for potential next request on same connection.

## Router and Middleware Placement

- Global middleware: `Radix::middleware_chain_`
- Route middleware: `RadixRoute::middleware_chain`
- Error handlers: string-keyed (`addErrorHandler(level, fn)`)

## Streams and Acceptors

`ServerImpl` owns an `Acceptor<T>` for each stream in template parameters.

- `Acceptor<PlainText>` reads `HTTP.PlainTextStream` config section
- `Acceptor<OpenSSLStream>` reads `HTTP.OpenSSLStream` config section

If config is empty, each acceptor falls back to built-in defaults.
