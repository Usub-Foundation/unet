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
- supports origin-form URI (`/path?query`)
- rejects URI fragments in request target
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
