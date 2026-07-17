# unet

C++23 networking library. HTTP/1.1, HTTP/2, WebSocket (RFC 6455 + RFC 8441 over h2), and a VLESS proxy example, all sitting on the [uvent](https://github.com/Usub-development/uvent) coroutine runtime.

Everything is coroutine-based. Handlers are `Awaitable<void>`. Request bodies stream through a channel you can `co_await` on. Responses can be sent whole, chunked, sendfile-zero-copy, or as a raw byte tunnel after protocol upgrade.

## What works today

- HTTP/1.1 server & client. Full RFC 9112 request-target parsing (origin, absolute, authority, asterisk forms), chunked transfer, keep-alive, Slowloris timeouts.
- HTTP/2 server (RFC 9113). HPACK, per-stream flow control, SETTINGS with extensions, CONTINUATION-storm defense, extended CONNECT (RFC 8441).
- WebSocket. Server side over h1 & h2 tunnel. `permessage-deflate` is not wired yet.
- VLESS proxy example that rides on the h2 tunnel path.
- Radix router with named params, inline regex constraints, wildcard tails.
- TLS via OpenSSL (POSIX) or SChannel (Windows), both with ALPN.

## What's missing

- HTTP/2 client.
- WebSocket compression (RFC 7692).
- HTTP/3.
- Response-phase middleware in the h1 send path (declared, not invoked).

## Minimal server

```cpp
#include <uvent/Uvent.h>
#include <unet/http.hpp>

usub::uvent::task::Awaitable<void>
hello(usub::unet::http::RequestReader &,
      usub::unet::http::ResponseWriter &res) {
    res.metadata.status_code = 200;
    res.headers.addHeader("content-type", "text/plain");
    co_await res.send(std::string{"hello\n"});
}

int main() {
    usub::Uvent runtime{4};
    usub::unet::http::ServerRadix server{runtime};
    server.handle("GET", "/hello", hello);
    runtime.run();
}
```

`ServerRadix` defaults to `127.0.0.1:22813` when you don't pass a config. Curl:

```
$ curl -i http://127.0.0.1:22813/hello
HTTP/1.1 200 OK
content-type: text/plain
content-length: 6

hello
```

For TLS, HTTP/2, WebSockets, upgrades, & the VLESS example, see [examples/HTTPServer.cpp](examples/HTTPServer.cpp) & [examples/VLESS.cpp](examples/VLESS.cpp) which cover every registered path the framework can serve.

## Build

```
git clone https://github.com/Usub-development/unet.git
cd unet
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DUNET_BUILD_EXAMPLES=ON
cmake --build build --parallel
```

Requires CMake 3.22+, GCC 13 / Clang 17 / MSVC 19.36+, & OpenSSL if you want the TLS examples on POSIX.

## Docs

- [docs/index.md](docs/index.md) - full documentation index
- [docs/quick-start.md](docs/quick-start.md) - first server in five minutes
- [docs/architecture.md](docs/architecture.md) - how the runtime is wired
- [docs/routing.md](docs/routing.md) - route patterns & constraints
- [docs/handler.md](docs/handler.md) - handler shapes, streaming, sendfile
- [docs/middlewares.md](docs/middlewares.md) - phases & invocation order
- [docs/client.md](docs/client.md) - HTTP client, keep-alive, proxies
- [docs/websockets.md](docs/websockets.md) - RFC 6455 & extended CONNECT
- [docs/upgrade.md](docs/upgrade.md) - custom protocols riding on HTTP/2

## License

MIT.
