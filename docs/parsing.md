# Parsing

HTTP/1 parsing is implemented as incremental state machines.

## Request Parsing

Type: `usub::unet::http::v1::RequestParser`

Primary APIs:

- `static parse(std::string_view)` for one-shot parse
- `step(Request&, begin, end)` for incremental parse

Parser emits `ParseStep` kinds:

- `CONTINUE`
- `HEADERS`
- `BODY`
- `COMPLETE`

### Validation Highlights

Current request parser behavior includes:

- requires `Host` header for HTTP/1.1 flow
- validates method token and version token
- accepts all four RFC 9112 §3.2 request-target forms:
  - **origin-form**: `/path?query` — for direct origin-server requests
  - **absolute-form**: `http://host:port/path?query` — typically proxy traffic; per
    RFC 9112 §3.3 an origin server treats authority/path as the request target
  - **authority-form**: `host:port` — only with `CONNECT`
  - **asterisk-form**: `*` — only with `OPTIONS`
- enforces RFC 9110 §16.1.1 method/form pairing: rejects `CONNECT` with
  origin-form, `*` with anything other than `OPTIONS`, etc.
- supports IPv4 / reg-name / bracketed IPv6 hosts in authority parsing
- accepts URI fragments on the wire and parses them (RFC 9110 §7.1 says clients
  MUST NOT send, but the parser is lenient; the fragment is captured into
  `metadata.uri.fragment`)
- enforces a single URI size budget across scheme + authority + path + query +
  fragment so an attacker can't split an oversized URI across sections
- supports content-length and chunked parsing
- validates conflicting or invalid `Content-Length`
- enforces method/URI/header limits via global settings

## Response Parsing

Type: `usub::unet::http::v1::ResponseParser`

Supports:

- status line parsing
- header parsing and validation
- body framing by content-length, chunked, or until-close mode

Used directly by `ClientImpl` to decode responses.

## Parse Errors

Common error container:

```cpp
struct ParseError {
    CODE code;
    STATUS_CODE expected_status;
    std::string message;
    std::array<char, 256> tail;
};
```

For handling guidance, see [Experimental Parse Error Notes](Experimental/parse_error_handling.md).

## Coverage Note

Tests under `tests/` provide behavior coverage signals, but they are not exhaustive.
