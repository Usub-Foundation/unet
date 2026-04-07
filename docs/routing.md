# Routing

Routing is implemented by `usub::unet::http::router::Radix`.

## Registering Routes

From server:

```cpp
server.handle("GET", "/health", handler);
server.handle(std::set<std::string>{"GET", "POST"}, "/items", handler);
server.handle(std::set<std::string>{"*"}, "/debug", handler);
```

The method wildcard is the literal `"*"` token.

## Path Pattern Features

Supported route path shapes:

- literal: `/health`
- named param: `/users/{id}`
- param with inline regex: `/orders/{id:[0-9]+}`
- wildcard tail: `/files/*`

Trailing slash is significant (`/users/{id}` and `/users/{id}/` are different routes).

## Param Constraints

You can pass a constraints map to overloads that accept it.

`param_constraint` type:

```cpp
struct param_constraint {
    std::string pattern;
    std::string description;
};
```

If no inline regex and no constraint is provided, default param regex is `[^/]+`.

## Handler Signatures

`RadixRoute::makeHandler(...)` accepts multiple forms and adapts them to a unified internal signature.

Supported forms:

- `Awaitable<void>(Request&, Response&)`
- `Awaitable<void>(Request&, Response&, RadixMatch&)`
- `Awaitable<void>(Request&, Response&, RadixMatch::UriParams&)`
- `Awaitable<void>(Request&, Response&, const RadixMatch::UriParams&)`

For class-member handlers, see [Handlers](handler.md#binding-class-member-handlers).

## Accessing URI Params

```cpp
usub::uvent::task::Awaitable<void>
getUser(usub::unet::http::Request&, usub::unet::http::Response& res,
        const usub::unet::http::router::RadixMatch::UriParams& params) {
    auto it = params.find("id");
    const std::string id = (it == params.end()) ? "missing" : it->second;
    res.setStatus(200).setBody("id=" + id + "\n");
    co_return;
}
```

You can also accept `RadixMatch&` directly and use `match.param("name")`.

## Error Handlers

Register error callbacks by string key:

```cpp
server.addErrorHandler("log", [](const Request&, Response&) {
    // logging or metrics
});

server.addErrorHandler("404", [](const Request&, Response& res) {
    res.setStatus(404).setBody("not found\n");
});
```

The runtime calls keys such as `"log"` and status string values (for example `"404"`).
