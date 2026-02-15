# Handlers

A route handler is a coroutine that returns `Awaitable<void>`.

## Canonical Shape

```cpp
usub::uvent::task::Awaitable<void>
handler(usub::unet::http::Request& req, usub::unet::http::Response& res) {
    (void)req;
    res.setStatus(200).setBody("ok\n");
    co_return;
}
```

## With Route Params

If your route has URI params, accept them as a third argument:

```cpp
usub::uvent::task::Awaitable<void>
handler(usub::unet::http::Request&, usub::unet::http::Response& res,
        const usub::unet::http::router::RadixMatch::UriParams& params) {
    auto id = params.contains("id") ? params.at("id") : "missing";
    res.setStatus(200).setBody("id=" + id + "\n");
    co_return;
}
```

## Practical Rules

- keep handlers non-blocking
- set status/headers/body explicitly
- use middleware for cross-cutting concerns
