# HTTP Client

`usub::unet::http::ClientImpl<Streams...>` sends HTTP requests. HTTP/1.1 only for now; the h2 client is on the roadmap.

## Core types

- `ClientImpl<Streams...>` - the client. Owns per-endpoint connection reuse & routes requests through one of its template streams.
- `ClientSession<VERSION::HTTP_1_1>` - internal per-connection parser session. You won't touch it directly.
- `ClientRequestOptions` - per-call options (proxy, timeout).
- `ClientProxyOptions` - proxy address & optional credentials.
- `ClientError` - failure code + optional message + optional parse detail.

`client.request(req, opts?)` returns `Awaitable<std::expected<Response, ClientError>>`. Success = a `Response`. Failure = a `ClientError` you can switch on.

## Building a request

`Request` is defined in `unet/http/core/request.hpp`. Minimum fields for a working request:

```cpp
usub::unet::http::Request req;
req.metadata.method_token       = "GET";
req.metadata.version            = usub::unet::http::VERSION::HTTP_1_1;
req.metadata.uri.scheme         = "https";
req.metadata.uri.authority.host = "example.com";
req.metadata.uri.authority.port = 443;
req.metadata.uri.path           = "/";
req.metadata.authority          = "example.com";   // used as Host header
```

Defaults the client fills in if you leave them empty:

| Field                   | Default                                            |
|-------------------------|----------------------------------------------------|
| `method_token`          | `"GET"`                                            |
| `version`               | `HTTP_1_1`                                         |
| `uri.path`              | `"/"`                                              |
| `uri.scheme`            | Inferred from the available template streams.      |
| `Host` header           | Filled from `authority` when missing.              |
| TLS SNI                 | Filled from `uri.authority.host` when not configured. |

## Sending & consuming

```cpp
using Client = usub::unet::http::ClientImpl<
    usub::unet::core::stream::PlainText,
    usub::unet::core::stream::OpenSSLStream<>
>;

usub::uvent::task::Awaitable<void>
fetch_example(Client &client) {
    usub::unet::http::Request req = build_request();

    auto result = co_await client.request(std::move(req));
    if (!result) {
        // result.error().code / .message / .parse_error
        std::cerr << "http error\n";
        co_return;
    }

    const auto &resp = *result;
    std::cout << "status " << resp.metadata.status_code << "\n";
    std::cout << resp.body << "\n";

    co_await client.close();   // close reusable connections
}
```

## Persistent connections

The client keeps one reusable connection per `{stream type, endpoint}`. Behavior:

- HTTP/1.1: reused by default unless either side sends `Connection: close`.
- HTTP/1.0: reused only when `Connection: keep-alive` is explicitly negotiated.
- `Keep-Alive: timeout=...` from the server is respected - idle connections past the timeout are closed & rebuilt.
- Responses framed "until close" are never reused.

Force-close all reusable connections:

```cpp
co_await client.close();
```

## Stream config (TLS)

For configurable streams like `OpenSSLStream<>`, set a client-managed default once:

```cpp
usub::unet::core::stream::OpenSSLStream<>::Config tls_cfg{};
tls_cfg.verify_peer = true;

client.setStreamConfig<usub::unet::core::stream::OpenSSLStream<>>(std::move(tls_cfg));
```

ALPN goes on the template arguments as string literals:

```cpp
using H2Tls        = usub::unet::core::stream::OpenSSLStream<"h2">;
using H2OrH1Tls    = usub::unet::core::stream::OpenSSLStream<"h2", "http/1.1">;
```

If `server_name` is empty in the managed config, the client fills it from `request.metadata.uri.authority.host` when opening a new TLS connection.

### Windows-native TLS

```cpp
using NativeTls = usub::unet::core::stream::SChannelStream<"http/1.1">;
```

Same managed-config pattern (`server_name`, `verify_peer`). Link `Secur32` & `Crypt32` yourself. ALPN template arguments accepted but native SChannel ALPN negotiation isn't wired in this first pass.

## Proxies

Per-request proxy through `ClientRequestOptions`:

```cpp
usub::unet::http::ClientRequestOptions opts{};
opts.proxy = usub::unet::http::ClientProxyOptions{
    .host = "127.0.0.1",
    .port = 8080,
    .username = "user",
    .password = "pass",
};

auto result = co_await client.request(std::move(req), opts);
```

Behavior:

- Plain HTTP over proxy: absolute-form request (`GET http://host:port/path HTTP/1.1`).
- HTTPS over proxy: `CONNECT host:port HTTP/1.1` first, TLS over the tunnel.
- Credentials become `Proxy-Authorization: Basic ...`.
- Proxy connections participate in the same persistence pool as direct connections.

## Timeouts

`ClientRequestOptions::connect_timeout_ms` bounds the connection dial. Read/write timeouts are inherited from the transport defaults on the stream config.

## Error codes

`ClientError::CODE`:

- `INVALID_REQUEST` - the `Request` was missing required fields (authority/host).
- `CONNECT_FAILED` - direct TCP/TLS connect failed.
- `PROXY_FAILED` - proxy dial or `CONNECT` response failed.
- `WRITE_FAILED` - socket write failed mid-request.
- `READ_FAILED` - socket read failed / connection went away mid-response.
- `PARSE_FAILED` - `ResponseParser` rejected the bytes. `parse_error` on the error object may carry line/state detail.
- `CLOSE_FAILED` - shutdown failed on an outgoing connection.

## Notes

- `Request` still has the legacy fluent `setBody` / `addHeader` helpers marked `[[deprecated]]` - use the public `metadata` / `headers` / `body` fields directly.
- Client is HTTP/1.1 only. A `Client::request` to an h2-only endpoint won't negotiate h2; it'll just try h1.
