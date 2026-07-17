# Request & Response

Handlers see two types, both non-copyable, both moved onto the handler frame: `RequestReader` (incoming) & `ResponseWriter` (outgoing). This page is the field & method reference.

## `RequestReader`

Defined in `unet/http/core/request.hpp`. Public fields:

| Field           | Type                          | Notes                                          |
|-----------------|-------------------------------|------------------------------------------------|
| `metadata`      | `RequestMetadata`             | method, uri, version, authority.               |
| `headers`       | `header::Headers`             | lowercase-normalized; multi-value.             |
| `trailers`      | `header::Headers`             | filled for h2 trailer HEADERS block.           |
| `peer`          | `PeerInfo`                    | ip, port, alpn, ssl. Marked experimental.      |
| `user_data`     | `std::any`                    | Handler / middleware scratch slot.             |

`RequestMetadata`:

- `method_token` (`std::string`, e.g. `"GET"`, `"POST"`, `"CONNECT"`).
- `uri` - `uri::URI` with `scheme`, `authority`, `path`, `query`, `fragment`.
- `version` - `VERSION::HTTP_1_0` / `HTTP_1_1` / `HTTP_2_0`.
- `authority` - `:authority` on h2, `Host` on h1.

`PeerInfo`:

- `ip` - dotted IPv4 or bracketed IPv6.
- `port` - `uint16_t`.
- `alpn` - `"h2"`, `"http/1.1"`, or empty.
- `ssl` - whether TLS terminated on our side.

### Body reads

| Method                                                | Returns                                                          |
|-------------------------------------------------------|------------------------------------------------------------------|
| `chunk()`                                             | Next chunk, or empty optional at EOF, or `BODY_ERROR`.           |
| `readBody()`                                          | Full body as one string (no size cap - risky on untrusted).      |
| `collect(limit)`                                      | Full body up to `limit`, or `FRAME_SIZE_ERROR` if bigger.        |
| `readBodyBytes(std::size_t n)`                        | Exactly `n` bytes as a string (may return less at EOF).          |
| `readBodyBytes(std::span<std::byte> dst)`             | Fill `dst`, return byte count written.                           |
| `eof()`                                               | True when no more bytes will arrive.                             |
| `getBodyChannel()` / `getBodyChannelPtr()`            | Raw channel access. Use only from framework code / upgrades.     |

Sample streaming read:

```cpp
usub::uvent::task::Awaitable<void>
consume(usub::unet::http::RequestReader &req,
        usub::unet::http::ResponseWriter &res) {
    for (;;) {
        auto chunk = co_await req.chunk();
        if (!chunk)          { res.metadata.status_code = 400; break; }
        if (!chunk->has_value()) break;   // EOF
        // handle **chunk here...
    }
    res.metadata.status_code = 200;
    co_await res.send(std::string{"done\n"});
}
```

### Query & headers

```cpp
auto q = req.metadata.uri.query;                        // raw string
auto ua = req.headers.value("user-agent");              // std::optional<std::string_view>
if (req.headers.contains("content-type")) { ... }
for (const auto &h : req.headers.all()) { ... }
```

Header names are already lowercased inside `Headers`. Pass any case in `value()` / `contains()`; the container normalizes on lookup.

### Query as a typed value

```cpp
template<typename T = std::string>
T RequestReader::getQueryAs();   // trivially constructs T from metadata.uri.query
```

For structured parsing, use the multipart or form-urlencoded utilities under `unet/mime/`.

## `ResponseWriter`

Defined in `unet/http/core/response.hpp`. Public fields:

| Field       | Type              | Notes                                            |
|-------------|-------------------|--------------------------------------------------|
| `metadata`  | `ResponseMetadata`| `version`, `status_code`, optional `status_message`. |
| `headers`   | `header::Headers` | Same container as request.                       |
| `trailers`  | `header::Headers` | Emitted after body on h2.                        |

Write in one of three shapes, picked by which method you call first.

### Fixed-size body

```cpp
res.metadata.status_code = 200;
res.headers.addHeader("content-type", "application/json");
co_await res.send(std::string{"{\"ok\":true}"});
```

`send(std::string)` emits headers + body. Framing (content-length on h1, DATA frames on h2) is done for you.

### Zero-copy file

```cpp
int fd = ::open(path, O_RDONLY);
res.metadata.status_code = 200;
res.headers.addHeader("content-type", "text/plain");
co_await res.file(fd, file_size);
::close(fd);
```

On POSIX this goes through `sendfile(2)` when available. Not through the response byte buffer.

### Chunked / streaming

```cpp
res.metadata.status_code = 200;
res.headers.addHeader("content-type", "text/event-stream");
if (!co_await res.start()) co_return;
for (auto ev : events) {
    if (!co_await res.chunk("data: " + ev + "\n\n")) co_return;
}
co_await res.end();
```

`start()` flushes headers, then `chunk(bytes)` / `chunk(fd, len, off)` writes body pieces, then `end()` closes the transfer. On h1 this becomes `Transfer-Encoding: chunked`. On h2 it's DATA frames with END_STREAM on the final one.

Every method returns `Awaitable<bool>`. `false` means the peer went away or the transport failed. Stop the handler at that point.

### Mode & aborts

`res.mode()` returns `Mode::Empty | Chunked | Sent | Aborted`. Any illegal transition (e.g. `chunk()` after `send()`) returns `false` without doing anything. `res.abort()` marks the writer aborted; on h2 this triggers a `RST_STREAM(CANCEL)` in `finalizeStreamResponse`.

## Notes

- Header names are canonicalized to lowercase inside the container. Whatever case you pass, storage & iteration are lowercase.
- `content-length` on `send(std::string)` is set for you. Don't add it manually.
- `content-type` is not defaulted. Add one, or the client will guess.
- The `RequestWriter` & `Request` types in the same header are client-side scaffolding pending a real client-side session. Don't rely on them yet.
