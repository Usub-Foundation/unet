# Webserver

**Fast and versatile web framework in modern C++**

Webserver is a lightweight, high-performance framework built on top of [Uvent](https://github.com/Usub-development/uvent).  
It supports HTTP/1.0, HTTP/1.1, and HTTP/2 (prior-knowledge and `Upgrade: h2c`), with HTTP/3 planned.

---

## Features
- 🚀 High-performance async event loop (via Uvent)
- 📦 RFC-compliant HTTP/1 parser (all four request-target forms)
- ⚡ HTTP/2 server with HPACK, multiplexed streams, SETTINGS exchange
- 🌲 Byte-level radix router with named params, inline regex constraints, named wildcards
- 🔌 Middleware system (header/body/response phases, global + per-route)
- 🧩 Modular and extensible design
- 🔒 TLS/SSL support (optional, OpenSSL or Windows SChannel)

---

## Quick Start

Minimal server:

```cpp
#include <cstdint>
#include <iostream>

#include <uvent/Uvent.h>

#include <unet/core/config.hpp>
#include <unet/http.hpp>
#include <unet/http/router/radix.hpp>

using Request = usub::unet::http::Request;
using Response = usub::unet::http::Response;

usub::uvent::task::Awaitable<void> handle_any(Request &, Response &response) {
    response.setStatus(200)
            .addHeader("content-type", "application/json")
            .setBody(R"({"status":"success"})");
    co_return;
}

usub::unet::core::Config make_server_config() {
    usub::unet::core::Config config{};

    usub::unet::core::Config::Object stream_cfg{};
    stream_cfg["host"] = usub::unet::core::Config::Value{std::string{"0.0.0.0"}};
    stream_cfg["port"] = usub::unet::core::Config::Value{static_cast<std::uint64_t>(45901)};
    stream_cfg["backlog"] = usub::unet::core::Config::Value{static_cast<std::uint64_t>(128)};
    stream_cfg["version"] = usub::unet::core::Config::Value{static_cast<std::uint64_t>(4)};
    stream_cfg["tcp"] = usub::unet::core::Config::Value{std::string{"tcp"}};

    usub::unet::core::Config::Object http_cfg{};
    http_cfg["PlainTextStream"] = usub::unet::core::Config::Value{std::move(stream_cfg)};
    config.root["HTTP"] = usub::unet::core::Config::Value{std::move(http_cfg)};

    return config;
}

int main() {
    usub::Uvent runtime{8};
    auto config = make_server_config();
    usub::unet::http::ServerRadix server{runtime, config};

    server.handle("*", "/", handle_any);

    std::cout << "minimal http server listening on http://127.0.0.1:45901\n";
    std::cout << "quick check: curl -i http://127.0.0.1:45901/\n";

    runtime.run();
    return 0;
}
```

Run:

```bash
curl http://127.0.0.1:45901/
```

---

## Documentation

Full documentation:
* [Getting Started Guide](https://usub-development.github.io/webserver/getting-started/)
* [Installation](https://usub-development.github.io/webserver/installation/)
* [Middleware](https://usub-development.github.io/webserver/middlewares/)
* [Request & Response](https://usub-development.github.io/webserver/request-response/)

---

## Roadmap

See [Roadmap](https://usub-development.github.io/webserver/roadmap/).

---

## Contributing

We welcome contributions! Please see the [Contributing Guide](https://usub-development.github.io/webserver/contributing/).

---

## License

MIT

