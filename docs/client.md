# HTTP Client

Client API is `usub::unet::http::ClientImpl<Streams...>`.

## Core Types

- `ClientImpl<...>`
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
    co_return;
}
```

## Error Codes

`ClientError::CODE` values:

- `INVALID_REQUEST`
- `CONNECT_FAILED`
- `WRITE_FAILED`
- `READ_FAILED`
- `PARSE_FAILED`
- `CLOSE_FAILED`

When parse fails, `parse_error` may include lower-level parse details.
