# Configuration

Server configuration lives in a `usub::unet::core::Config` object. Pass it to `ServerImpl`'s constructor. Any section you don't provide falls back to the acceptor's built-in defaults.

## Config shape

`Config::root` is a `std::unordered_map<std::string, Value>` where `Value` holds `std::string`, `std::uint64_t`, `std::int64_t`, `bool`, or nested `Object` (another map). Read with `getString / getUInt / getInt / getBool`. Dotted paths walk nested objects.

The server looks up top-level `HTTP.<StreamName>` for each stream type in the template pack.

## Sections

### `HTTP.PlainTextStream`

| Key       | Type   | Default       | Notes                                    |
|-----------|--------|---------------|------------------------------------------|
| `host`    | string | `127.0.0.1`   | Bind address.                            |
| `port`    | uint   | `22813`       | TCP port.                                |
| `backlog` | int    | `50`          | `listen()` backlog.                      |
| `version` | int    | `4`           | `4` for IPv4, `6` for IPv6.              |
| `tcp`     | string | `tcp`         | `tcp` or `udp`.                          |
| `base_timeout` | uint | `20000`   | ms, initial socket read timeout at accept. |

### `HTTP.OpenSSLStream`

Same keys as above, plus:

| Key    | Type   | Default   | Notes                                        |
|--------|--------|-----------|----------------------------------------------|
| `port` | uint   | `443`     | Overrides the plaintext default.             |
| `cert` | string | `cert.pem`| PEM certificate path.                        |
| `key`  | string | `key.pem` | PEM private key path.                        |

### `HTTP.SChannelStream` (Windows only)

Same shape, but the TLS material comes from a PFX bundle:

| Key         | Type   | Default      | Notes                        |
|-------------|--------|--------------|------------------------------|
| `pfx`       | string | `server.pfx` | PKCS#12 bundle path.         |
| `password`  | string | *(empty)*    | Bundle password if any.      |

### Top-level flags

| Key            | Type | Default | Notes                                                              |
|----------------|------|---------|--------------------------------------------------------------------|
| `HTTP.enable_h2c` | bool | `false` | Enables h2 prior-knowledge sniff on plaintext connections + h2c upgrade path on h1. |

## Example: plaintext + TLS

```cpp
#include <unet/core/config.hpp>

using Cfg = usub::unet::core::Config;

Cfg makeConfig() {
    Cfg cfg;

    Cfg::Object plain;
    plain.emplace("host", Cfg::Value{std::string{"0.0.0.0"}});
    plain.emplace("port", Cfg::Value{static_cast<std::uint64_t>(8080)});

    Cfg::Object tls;
    tls.emplace("host", Cfg::Value{std::string{"0.0.0.0"}});
    tls.emplace("port", Cfg::Value{static_cast<std::uint64_t>(8443)});
    tls.emplace("cert", Cfg::Value{std::string{"cert.pem"}});
    tls.emplace("key",  Cfg::Value{std::string{"key.pem"}});

    Cfg::Object http;
    http.emplace("PlainTextStream", Cfg::Value{std::move(plain)});
    http.emplace("OpenSSLStream",   Cfg::Value{std::move(tls)});

    cfg.root.emplace("HTTP", Cfg::Value{std::move(http)});
    return cfg;
}
```

Then wire it into a server that carries both streams:

```cpp
usub::unet::http::ServerImpl<
    usub::unet::http::router::Radix,
    usub::unet::core::stream::PlainText,
    usub::unet::core::stream::OpenSSLStream<"h2", "http/1.1">
> server{runtime, makeConfig()};
```

The ALPN template arguments on `OpenSSLStream` are the protocols this server will advertise, in preference order. Include `"h2"` if you want HTTP/2 negotiation.

## Runtime tuning (Connection defaults)

Some limits live on the per-connection `v1::Connection` / `v2::Connection`, not in `Config`. Set them per-connection through the lifecycle hook:

```cpp
server.onHTTP1Connection([](usub::unet::http::v1::Connection &c){
    c.keep_alive_max_requests   = 100;
    c.max_keep_alive_timeout    = std::chrono::milliseconds{15000};
    c.default_keep_alive_timeout = std::chrono::milliseconds{10000};
});

server.onHTTP2Connection([](usub::unet::http::v2::Connection &c){
    c.max_continuations_per_header_block = 8;
    c.idle_timeout = std::chrono::milliseconds{30000};
});
```

Full field lists live in `include/unet/http/v1/connection.hpp` & `include/unet/http/v2/connection.hpp`.
