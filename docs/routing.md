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

The router is a byte-level compressed radix trie. Patterns support:

- **literal**: `/health` — common prefixes are merged across segment boundaries,
  so `/users` and `/userdata` share the `/user` edge internally.
- **named param**: `/users/{id}` — captures one segment by default (`[^/]+`).
- **param with inline regex**: `/orders/{id:[0-9]+}` — constrains the capture.
- **named wildcard tail**: `/files/*splat` — captures everything remaining,
  must be the last token. Bare `*` is NOT a wildcard — it's a literal byte
  (which is exactly what makes `server.handle("OPTIONS", "*", fn)` register
  the asterisk-form request-target for `OPTIONS *` without any special API).
- **escape**: `\{`, `\}`, `\*`, `\\` for literal occurrences mid-pattern.

Trailing slash is significant (`/users/{id}` and `/users/{id}/` are different routes).

## Param Map Type

Captured params are surfaced as:

```cpp
using UriParams = std::unordered_map<std::string_view, std::string_view>;
```

Both keys and values are `string_view` slices over the request's path — no
allocation in the hot path. If your handler needs an owning `std::string`,
construct one explicitly: `std::string{params.at("id")}`.

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
    std::string id = (it == params.end()) ? std::string{"missing"}
                                          : std::string{it->second};
    res.setStatus(200).setBody("id=" + id + "\n");
    co_return;
}
```

You can also accept `RadixMatch&` directly and use `match.param("name")`, which
returns `std::optional<std::string_view>`.

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
