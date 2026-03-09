# HTTP Client

Client API is `usub::unet::http::ClientImpl<Streams...>`.

## Core Types

- `ClientImpl<...>`
- `ClientSession<VERSION::HTTP_1_1>` (internal protocol session used by the client runtime)
- `ClientProxyOptions`
- `ClientRequestOptions` (connection timeout)
- `ClientError`

`request(...)` returns:

- `Awaitable<std::expected<Response, ClientError>>`

## Required Request Fields

Set at minimum:

- `request.metadata.authority` (required)
- `request.metadata.uri.authority.host`

Common explicit fields:

- `method_token` (`GET`, `POST`, ...)
- `uri.scheme` (`http` / `https`)
- `uri.path`
- `uri.authority.port`

## Defaults Applied By Client

If omitted, current client logic defaults:

- method -> `GET`
- version -> `HTTP/1.1`
- path -> `/`
- scheme -> inferred from available stream types
- `host` header -> auto-added when missing and authority host is available
- HTTP/1.1 connections are kept alive by default when the server allows it
- HTTP/1.0 connections are only reused when keep-alive is explicitly negotiated
- `Keep-Alive: timeout=...` is respected by expiring idle client connections before reuse
- TLS client hostname / SNI is derived from the request host when no explicit managed stream config is provided
- Optional HTTP proxy support is available for both plain HTTP requests and HTTPS `CONNECT` tunneling

## Persistent Connections

The client now keeps one reusable connection per stream type and target endpoint.

- Reuse is attempted automatically for HTTP/1.1 unless either side sends `Connection: close`
- For HTTP/1.0, reuse only happens when `Connection: keep-alive` is negotiated
- Responses that are framed `until close` are never reused
- If an idle keep-alive timeout has elapsed, the client closes and reconnects before the next request

You can explicitly close any live client-side connections with:

```cpp
co_await client.close();
```

## Session Structure

The client runtime now delegates HTTP/1.1 response parsing to a dedicated session implementation.

- `ClientImpl` owns connection reuse, proxy routing, and stream selection
- `ClientSession<VERSION::HTTP_1_1>` owns incremental HTTP/1.1 response parsing through `onBytes(...)` / `onClose()`

## Managed Stream Config

For configurable streams such as `OpenSSLStream`, you can set a default client-managed config once:

```cpp
usub::unet::core::stream::OpenSSLStream::Config tls_cfg{};
tls_cfg.verify_peer = true;

client.setStreamConfig<usub::unet::core::stream::OpenSSLStream>(std::move(tls_cfg));
```

If `server_name` is left empty, the client fills it from `request.metadata.uri.authority.host` when opening a new TLS connection.
Prefer `client.setStreamConfig<...>(...)` for client-side TLS defaults so the client can keep that config aligned with connection reuse.

## Proxy Support

Set a proxy per request through `ClientRequestOptions`:

```cpp
usub::unet::http::ClientRequestOptions options{};
options.proxy = usub::unet::http::ClientProxyOptions{
    .host = "127.0.0.1",
    .port = 8080,
    .username = "user",
    .password = "pass",
};
```

Current client proxy behavior:

- Plain HTTP over proxy: sends absolute-form requests, for example `GET http://host:port/path HTTP/1.1`
- HTTPS over proxy: opens a TCP connection to the proxy, sends `CONNECT host:port HTTP/1.1`, then runs TLS over the established tunnel
- If credentials are present, the client sends `Proxy-Authorization: Basic ...`
- Proxy connections participate in the same client-side persistence logic as direct connections

## Plain + TLS Example

```cpp
using Client = usub::unet::http::ClientImpl<
    usub::unet::core::stream::PlainText,
    usub::unet::core::stream::OpenSSLStream
>;

usub::uvent::task::Awaitable<void> run(Client& client) {
    usub::unet::http::Request req{};
    req.metadata.method_token = "GET";
    req.metadata.version = usub::unet::http::VERSION::HTTP_1_1;
    req.metadata.uri.scheme = "https";
    req.metadata.uri.authority.host = "example.com";
    req.metadata.uri.authority.port = 443;
    req.metadata.authority = "example.com";
    req.metadata.uri.path = "/";

    auto result = co_await client.request(std::move(req));
    if (!result) {
        // inspect result.error().code / .message
        co_return;
    }

    const auto status = result->metadata.status_code;
    const auto& body = result->body;
    (void)status;
    (void)body;

    co_await client.close();
    co_return;
}
```

## Error Codes

`ClientError::CODE` values:

- `INVALID_REQUEST`
- `CONNECT_FAILED`
- `PROXY_FAILED`
- `WRITE_FAILED`
- `READ_FAILED`
- `PARSE_FAILED`
- `CLOSE_FAILED`

When parse fails, `parse_error` may include lower-level parse details.
