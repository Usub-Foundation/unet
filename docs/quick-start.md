# Quick Start

The smallest working server, then a few useful additions.

## 1. Minimal server

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

`ServerRadix` with no config binds `127.0.0.1:22813`, backlog 50, IPv4. Enough for a smoke test.

```
$ curl -i http://127.0.0.1:22813/hello
HTTP/1.1 200 OK
content-type: text/plain
content-length: 6

hello
```

## 2. Route params

Add a third parameter to the handler & the router will fill it:

```cpp
using UriParams = usub::unet::http::router::RadixMatch::UriParams;

usub::uvent::task::Awaitable<void>
greet(usub::unet::http::RequestReader &,
      usub::unet::http::ResponseWriter &res,
      const UriParams &params) {
    std::string_view name{"stranger"};
    if (auto it = params.find("name"); it != params.end()) name = it->second;

    res.metadata.status_code = 200;
    std::string body = "hello, ";
    body.append(name);
    body += "\n";
    co_await res.send(std::move(body));
}

// ...
server.handle("GET", "/greet/{name}", greet);
```

`UriParams` values are `string_view`s into the request path. Copy them if you need to outlive the handler frame.

## 3. Streaming response

For chunked responses, use `start` / `chunk` / `end`:

```cpp
usub::uvent::task::Awaitable<void>
ticks(usub::unet::http::RequestReader &,
      usub::unet::http::ResponseWriter &res) {
    res.metadata.status_code = 200;
    res.headers.addHeader("content-type", "text/plain");
    if (!co_await res.start()) co_return;
    for (int i = 0; i < 5; ++i) {
        if (!co_await res.chunk("tick " + std::to_string(i) + "\n")) co_return;
        co_await usub::uvent::system::this_coroutine::sleep_for(std::chrono::milliseconds{300});
    }
    co_await res.end();
}
```

Client sees each line the moment it's sent, not queued at the end. Verify with `curl -N`.

## 4. Reading the request body

```cpp
usub::uvent::task::Awaitable<void>
echo(usub::unet::http::RequestReader &req,
     usub::unet::http::ResponseWriter &res) {
    constexpr std::size_t kMax = 1 << 20;
    auto body = co_await req.collect(kMax);
    if (!body) { res.metadata.status_code = 413; co_await res.send(std::string{"too big\n"}); co_return; }
    res.metadata.status_code = 200;
    co_await res.send(std::move(*body));
}
```

`collect(limit)` reads until EOF or the limit. For true streaming reads use `req.chunk()` in a loop.

## 5. Configure the listener

Default port not what you want? Or you want TLS? Build a `Config`:

```cpp
using Cfg = usub::unet::core::Config;

Cfg cfg;
Cfg::Object plain;
plain.emplace("host", Cfg::Value{std::string{"0.0.0.0"}});
plain.emplace("port", Cfg::Value{static_cast<std::uint64_t>(8080)});

Cfg::Object http;
http.emplace("PlainTextStream", Cfg::Value{std::move(plain)});
cfg.root.emplace("HTTP", Cfg::Value{std::move(http)});

usub::unet::http::ServerRadix server{runtime, cfg};
```

Full config keys: [Configuration](config.md).

## 6. Build

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Or `-DCMAKE_BUILD_TYPE=Debug` for `-O0 -g` when debugging.

## Where to go next

- [Handlers](handler.md) - all handler shapes, class-member handlers, sendfile.
- [Routing](routing.md) - pattern grammar & constraints.
- [Middlewares](middlewares.md) - HEADER/BODY phases, per-route registration.
- [WebSockets](websockets.md) - upgrade & broadcast.
- [Request & Response](request-response.md) - full field & method reference.
