# Routing

`usub::unet::http::router::Radix` is a byte-level compressed radix trie. Common prefixes get merged across segment boundaries (`/users` & `/userdata` share a `/user` edge internally). O(pattern-length) match, no allocation on the hit path.

## Registering routes

```cpp
server.handle("GET", "/health", handler);
server.handle(std::set<std::string>{"GET", "POST"}, "/items", handler);
server.handle(std::set<std::string>{"*"}, "/debug", handler);
```

Overloads:

- Single method as `std::string` / `const char *`.
- Multi-method as `std::set<std::string>`.
- Literal `"*"` as the method-wildcard token.

## Pattern grammar

| Piece                     | Example                    | Meaning                                                       |
|---------------------------|----------------------------|---------------------------------------------------------------|
| literal                   | `/health`                  | Exact bytes.                                                  |
| named param               | `/users/{id}`              | Captures one segment (default `[^/]+`).                       |
| param + inline regex      | `/orders/{id:[0-9]+}`      | Constrains capture. Anchored implicitly.                      |
| named wildcard tail       | `/files/*splat`            | Captures everything remaining. Must be the last token.        |
| escape                    | `\{`, `\}`, `\*`, `\\`     | Literal `{`, `}`, `*`, `\`.                                   |

Bare `*` is NOT a wildcard - it's a literal byte. Which is exactly what makes `server.handle("OPTIONS", "*", fn)` register the asterisk-form request-target from RFC 9110 §7.1 without any special API.

Trailing slash matters. `/users/{id}` & `/users/{id}/` are different routes.

## Reading captured params

`RadixMatch::UriParams` is `std::unordered_map<std::string_view, std::string_view>`. Keys & values are slices over the request path buffer.

```cpp
using UriParams = usub::unet::http::router::RadixMatch::UriParams;

usub::uvent::task::Awaitable<void>
show(usub::unet::http::RequestReader &,
     usub::unet::http::ResponseWriter &res,
     const UriParams &params) {
    auto it = params.find("id");
    if (it == params.end()) { res.metadata.status_code = 400; co_await res.send(std::string{"no id\n"}); co_return; }

    std::string body = "id=";
    body.append(it->second);
    body += "\n";
    res.metadata.status_code = 200;
    co_await res.send(std::move(body));
}
```

If you plan to store the value beyond the handler frame, copy it into a `std::string`.

You can also accept `RadixMatch &` directly & call `match.param("name")` which returns `std::optional<std::string_view>`.

## Param constraints

Two ways to constrain a param:

1. Inline in the pattern: `/orders/{id:[0-9]+}`.
2. External constraints map passed to the overload that accepts one:

   ```cpp
   struct param_constraint {
       std::string pattern;
       std::string description;
   };
   ```

Default constraint (no inline regex, no external constraint) is `[^/]+` - one segment, no slash.

## Handler signatures

`RadixRoute::makeHandler` adapts these shapes to one internal signature:

- `Awaitable<void>(RequestReader&, ResponseWriter&)`
- `Awaitable<void>(RequestReader&, ResponseWriter&, RadixMatch&)`
- `Awaitable<void>(RequestReader&, ResponseWriter&, RadixMatch::UriParams&)`
- `Awaitable<void>(RequestReader&, ResponseWriter&, const RadixMatch::UriParams&)`

Class members go through `std::bind_front(&Class::method, &instance)`. See [Handlers](handler.md#class-member-handlers).

## Upgrade routes

Routes that upgrade the connection (WebSocket, VLESS, custom protocols) register with `handleUpgrade`:

```cpp
server.handleUpgrade("POST", "/vless", vless_handler);
server.handleUpgrade(std::set<std::string>{"GET", "CONNECT"}, "/ws",
                     usub::unet::ws::upgradeHandler(chat_handler));
```

The handler gets `UpgradeContext &` as a third argument. Full mechanism: [Upgrade routes](upgrade.md).

## Error handlers

String-keyed, registered per server, invoked when the runtime produces an error status. Key is the status-code string (`"404"`, `"405"`, `"408"`, `"500"`, ...).

```cpp
server.addErrorHandler("404",
    [](usub::unet::http::RequestReader &,
       usub::unet::http::ResponseWriter &res) -> usub::uvent::task::Awaitable<void> {
        res.headers.addHeader("content-type", "application/json");
        co_await res.send(std::string{"{\"error\":\"not_found\"}\n"});
    });
```

Registering the same key twice overwrites - `addErrorHandler` uses `insert_or_assign`.

### Default fallback

Unhandled status codes don't hang the connection. The router falls back to `usub::unet::http::defaultErrorResponse` which emits `"<code> <status message>\n"` with `content-type: text/plain; charset=utf-8`, e.g. `"404 Not Found\n"`.

The fallback only runs when the response is still `Mode::Empty` - it never overwrites bytes a handler already wrote.

Override any specific code with `addErrorHandler("<code>", yourHandler)`. Leave the rest alone.
