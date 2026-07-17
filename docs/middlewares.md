# Middlewares

Middleware is any coroutine matching this signature:

```cpp
usub::uvent::task::Awaitable<bool>(RequestReader &, ResponseWriter &)
```

Return `true` to continue the chain. Return `false` to stop it - the handler won't run, & the response (whatever you wrote before returning `false`) is what the client gets.

## Phases

```cpp
enum class MIDDLEWARE_PHASE { HEADER, BODY, RESPONSE };
```

| Phase      | Fires                                          | Invoked today?          |
|------------|------------------------------------------------|-------------------------|
| `HEADER`   | Right after headers parse, before the handler. | Yes.                    |
| `BODY`     | After each body chunk on the h1 body-read loop.| Yes on h1.              |
| `RESPONSE` | Around the response send path.                 | Not yet on h1. See roadmap. |

Register globally (server-wide) or per-route (only for that route). Global HEADER middleware runs before route match, so it sees every request. Route middleware runs after the match, so it can rely on `RadixMatch`.

## Registering

Global:

```cpp
server.addMiddleware(usub::unet::http::MIDDLEWARE_PHASE::HEADER, log_request);
server.addMiddleware(usub::unet::http::MIDDLEWARE_PHASE::HEADER, require_auth);
```

Per route:

```cpp
server.handle("GET", "/admin", admin_handler)
      .addMiddleware(usub::unet::http::MIDDLEWARE_PHASE::HEADER, require_admin_role);
```

Order: chain order == registration order.

## A logging middleware

```cpp
usub::uvent::task::Awaitable<bool>
log_request(usub::unet::http::RequestReader &req,
            usub::unet::http::ResponseWriter &) {
    std::cerr << "[req] " << req.metadata.method_token
              << " " << req.metadata.uri.path
              << " from " << req.peer.ip << ":" << req.peer.port << "\n";
    co_return true;
}
```

## An auth middleware that rejects early

```cpp
usub::uvent::task::Awaitable<bool>
require_secret(usub::unet::http::RequestReader &req,
               usub::unet::http::ResponseWriter &res) {
    if (req.metadata.uri.path != "/secret") co_return true;
    auto header = req.headers.value("x-auth");
    if (header && *header == "please") co_return true;

    res.metadata.status_code = 401;
    res.headers.addHeader("content-type", "text/plain");
    co_await res.send(std::string{"unauthorized\n"});
    co_return false;
}
```

Returning `false` after writing the 401 body: the handler is skipped, the response goes as written.

## Passing context between middleware & handler

`RequestReader::user_data` is a `std::any` you can freely populate:

```cpp
struct AuthedUser { std::string tenant_id; };

usub::uvent::task::Awaitable<bool>
attach_user(usub::unet::http::RequestReader &req,
            usub::unet::http::ResponseWriter &) {
    req.user_data = AuthedUser{"acme-corp"};
    co_return true;
}

usub::uvent::task::Awaitable<void>
handler(usub::unet::http::RequestReader &req,
        usub::unet::http::ResponseWriter &res) {
    auto *user = std::any_cast<AuthedUser>(&req.user_data);
    // ...
    co_await res.send(std::string{"ok\n"});
}
```

Type-safe within one request lifecycle. Reset per request.

## Connection & stream hooks

Not middleware, but related. Fire once per connection / stream, not per request:

```cpp
server.onHTTP1Connection([](usub::unet::http::v1::Connection &c) {
    // Tune keep-alive, timeouts, whatever.
});

server.onHTTP2Connection([](usub::unet::http::v2::Connection &c) {
    c.max_continuations_per_header_block = 8;
});

server.onHTTP2Stream([](usub::unet::http::v2::Connection &c, std::uint32_t sid) {
    std::cerr << "new h2 stream " << sid << " on " << c.peer.ip << "\n";
});
```

## Error handlers

Not middleware. Registered by string key equal to the HTTP status (`"404"`, `"405"`, `"408"`, `"500"`, ...), invoked when the runtime produces that error status:

```cpp
server.addErrorHandler("404", [](RequestReader &, ResponseWriter &res) -> Awaitable<void> {
    res.headers.addHeader("content-type", "application/json");
    co_await res.send(std::string{"{\"error\":\"not_found\"}\n"});
});
```

Codes you don't register a handler for get a default plain-text response - see [Routing / Error handlers](routing.md#error-handlers). Handlers you do register overwrite any previous registration for the same code (`insert_or_assign`).

## Current limitations

- `Radix::addMiddleware(...)` only accepts global registration for the `HEADER` phase. Non-HEADER global registration logs a warning & is ignored. Per-route registration accepts any phase.
- `RESPONSE` phase middleware never fires in the h1 send path. Declared, not wired. See [Roadmap](roadmap.md).
