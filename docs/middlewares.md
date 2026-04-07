# Middlewares

Middleware functions intercept request/response handling phases.

## Signature

```cpp
bool(Request&, Response&)
```

Return value:

- `true`: continue processing
- `false`: stop middleware/handler flow for that phase chain

## Phases

`MIDDLEWARE_PHASE` values:

- `HEADER`
- `BODY`
- `RESPONSE`

## Registering Middleware

Global middleware:

```cpp
server.addMiddleware(usub::unet::http::MIDDLEWARE_PHASE::HEADER, globalHeaderMw);
```

Route middleware:

```cpp
server.handle("GET", "/hello", handler)
      .addMiddleware(usub::unet::http::MIDDLEWARE_PHASE::HEADER, routeHeaderMw)
      .addMiddleware(usub::unet::http::MIDDLEWARE_PHASE::BODY, routeBodyMw);
```

## Current Runtime Behavior (Important)

In current `HTTP/1.1` session logic:

- header middleware is executed
- body middleware is executed
- response middleware is defined in API but is not currently invoked in the HTTP/1 send path

Global middleware limitation in current radix implementation:

- `Radix::addMiddleware(...)` supports global `HEADER` phase only
- non-header global registration logs a warning and is ignored

## Request-Scoped User Data

`Request` includes:

```cpp
std::any user_data;
```

Use this to pass context between middleware and handlers during one request lifecycle.
