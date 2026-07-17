# Parsing

The HTTP/1 wire parsers are incremental state machines. Feed them bytes; they either advance state, return control asking for more bytes, or fail with a `ParseError`.

## HTTP/1 request parser

`usub::unet::http::v1::RequestParser` in `include/unet/http/v1/wire/request_parser.hpp`.

Primary entry:

```cpp
std::expected<void, ParseError>
step(RequestReader &request,
     std::string_view::const_iterator &begin,
     const std::string_view::const_iterator end);
```

`begin` is advanced in place. Call `step` repeatedly with `[begin, end)` from your socket buffer until the state reaches `HEADERS_DONE` (headers ready, dispatch the handler) or `COMPLETE` (whole request done).

### State machine (subset)

- `METHOD_TOKEN` -> `URI` -> `ORIGIN_PATH` / `ABSOLUTE_FORM` / `AUTHORITY_FORM` / `ASTERISK_FORM` -> `VERSION` -> header pairs -> `HEADERS_VALIDATION` -> `HEADERS_DONE`.
- Body branches out of `HEADERS_DONE`: `DATA_CONTENT_LENGTH` for content-length bodies, `DATA_CHUNKED_*` for chunked transfer.
- `COMPLETE` when the body reaches EOF.
- `FAILED` if any check fails.

### What it enforces

- All four RFC 9112 §3.2 request-target forms:
  - origin-form (`/path?query`)
  - absolute-form (`http://host:port/path`)
  - authority-form (`host:port`, only with `CONNECT`)
  - asterisk-form (`*`, only with `OPTIONS`)
- RFC 9110 §16.1.1 method/form pairing (rejects `CONNECT` in origin-form etc.).
- IPv4, reg-name, & bracketed IPv6 hosts in authority parsing.
- URI fragment is captured into `metadata.uri.fragment` even though RFC 9110 §7.1 says clients MUST NOT send one.
- Single URI size budget across scheme + authority + path + query + fragment - can't split a giant URI across sections to sneak past a per-part limit.
- `Content-Length`: multiple identical values allowed, conflicting values rejected.
- `Transfer-Encoding: chunked` accepted. Other encodings rejected. Both `Content-Length` & `Transfer-Encoding` together = 400.
- `Host` header required for HTTP/1.1.
- Global limits: `max_headers_size` (256 KiB), `max_method_token_size` (uint8 max), `max_uri_size` (uint16 max).

## HTTP/1 response parser

`usub::unet::http::v1::ResponseParser` in `include/unet/http/v1/wire/response_parser.hpp`. Same shape as the request parser, used by the client.

Body framing modes: content-length, chunked, until-close.

## HTTP/2 request parser

`usub::unet::http::v2::RequestParser` in `include/unet/http/v2/wire/request_parser.hpp` is smaller in scope than the h1 parser because framing is handled at the frame layer. It only tracks the header-block phases: `HEADERS`, `HEADERS_DONE`, `TRAILERS`, `DONE`, `FAILED`.

Body bytes bypass the parser entirely; they stream through `request.getBodyChannel()`. The parser only owns the HPACK accumulator & the content-length invariant (RFC 9113 §8.1.2.6).

## `ParseError`

```cpp
struct ParseError {
    CODE code;                       // GENERIC_ERROR for now; more later
    STATUS_CODE expected_status;     // suggested HTTP status to return
    std::string message;             // human-readable
    std::array<char, 256> tail;      // last bytes seen, for context
};
```

The session uses `expected_status` for the response status when a request parse fails, then invokes the matching error handler.

## Coverage

Tests under `tests/` exercise the parsers. Coverage is useful, not exhaustive. When you fix a parser bug, add a regression test alongside.
