# Configuration

Current server configuration is passed as a `usub::unet::core::Config` object.

The old `[server]` + `[[listener]]` shape used in legacy docs/examples is not the active shape for the current HTTP server implementation.

## Active Config Sections

### `HTTP.PlainTextStream`

Recognized keys:

- `host` (string, default `127.0.0.1`)
- `port` (uint, default `22813`)
- `backlog` (int, default `50`)
- `version` (int, `4` or `6`, default `4`)
- `tcp` (string, `tcp` or `udp`, default `tcp`)

### `HTTP.OpenSSLStream`

Recognized keys:

- `host` (string, default `127.0.0.1`)
- `port` (uint, default `443`)
- `backlog` (int, default `50`)
- `version` (int, `4` or `6`, default `4`)
- `tcp` (string, `tcp` or `udp`, default `tcp`)
- `key` (string path, default `key.pem`)
- `cert` (string path, default `cert.pem`)

## Minimal Programmatic Config Example

```cpp
#include <unet/core/config.hpp>

using Cfg = usub::unet::core::Config;

static Cfg::Value s(std::string v) { Cfg::Value x; x.data = std::move(v); return x; }
static Cfg::Value u(std::uint64_t v) { Cfg::Value x; x.data = v; return x; }
static Cfg::Value o(Cfg::Object v) { Cfg::Value x; x.data = std::move(v); return x; }

Cfg makeConfig() {
    Cfg cfg;

    Cfg::Object plain;
    plain.emplace("host", s("127.0.0.1"));
    plain.emplace("port", u(8080));
    plain.emplace("backlog", u(128));
    plain.emplace("version", u(4));
    plain.emplace("tcp", s("tcp"));

    Cfg::Object tls;
    tls.emplace("host", s("0.0.0.0"));
    tls.emplace("port", u(8443));
    tls.emplace("backlog", u(128));
    tls.emplace("version", u(4));
    tls.emplace("tcp", s("tcp"));
    tls.emplace("key", s("server.key"));
    tls.emplace("cert", s("server.crt"));

    Cfg::Object http;
    http.emplace("PlainTextStream", o(std::move(plain)));
    http.emplace("OpenSSLStream", o(std::move(tls)));

    cfg.root.emplace("HTTP", o(std::move(http)));
    return cfg;
}
```

## Use With Server

```cpp
usub::unet::core::Config cfg = makeConfig();
usub::unet::http::ServerImpl<
    usub::unet::http::router::Radix,
    usub::unet::core::stream::PlainText,
    usub::unet::core::stream::OpenSSLStream
> server{cfg};
```

## Notes

- Missing keys are defaulted by each acceptor implementation.
- Values are read with `Config::getString/getUInt/getInt` helpers.
- Dotted paths are resolved as nested objects (for example `HTTP.PlainTextStream`).
