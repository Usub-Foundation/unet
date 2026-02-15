# Quick Start

This page shows the shortest working server flow with the current API.

## 1. Minimal HTTP Server

```cpp
#include <uvent/Uvent.h>
#include <unet/http.hpp>

using usub::unet::http::Request;
using usub::unet::http::Response;

usub::uvent::task::Awaitable<void> hello(Request&, Response& res) {
    res.setStatus(usub::unet::http::STATUS_CODE::OK)
       .addHeader("content-type", "text/plain")
       .setBody("Hello from unet\n");
    co_return;
}

int main() {
    usub::Uvent runtime{4};

    usub::unet::http::ServerRadix server;
    server.handle("GET", "/hello", hello);

    runtime.run();
    return 0;
}
```

Default `ServerRadix` listener config (when no explicit config is provided):

- Host: `127.0.0.1`
- Port: `22813`
- Backlog: `50`

## 2. Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

## 3. Test

```bash
curl -i http://127.0.0.1:22813/hello
```

Expected status: `200 OK`

## 4. Next Steps

- [Configuration](config.md)
- [Routing](routing.md)
- [Middlewares](middlewares.md)
- [Request and Response](request-response.md)
