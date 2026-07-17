# Handlers

A route handler is a coroutine that returns `Awaitable<void>`, takes a `RequestReader &` & a `ResponseWriter &`, & optionally a params argument. The framework spawns it after the router matches & the HEADER middleware chain passes.

## Basic shape

```cpp
usub::uvent::task::Awaitable<void>
handler(usub::unet::http::RequestReader &,
        usub::unet::http::ResponseWriter &res) {
    res.metadata.status_code = 200;
    res.headers.addHeader("content-type", "text/plain");
    co_await res.send(std::string{"ok\n"});
}
```

Register:

```cpp
server.handle("GET", "/health", handler);
server.handle(std::set<std::string>{"GET", "POST"}, "/items", handler);
```

The set overload registers the same handler for multiple methods. The literal string `"*"` is the method wildcard.

## With URI params

Add a third parameter. The framework fills it from the router match.

```cpp
using UriParams = usub::unet::http::router::RadixMatch::UriParams;

usub::uvent::task::Awaitable<void>
show_user(usub::unet::http::RequestReader &,
          usub::unet::http::ResponseWriter &res,
          const UriParams &params) {
    std::string_view id{"missing"};
    if (auto it = params.find("id"); it != params.end()) id = it->second;

    res.metadata.status_code = 200;
    std::string body = "id=";
    body.append(id);
    body += "\n";
    co_await res.send(std::move(body));
}

server.handle("GET", "/users/{id}", show_user);
```

`UriParams` values are `string_view`s into the request's `path` buffer. They stay valid for the handler frame. Copy into `std::string` if you spawn detached work.

## Class-member handlers

Bind an instance with `std::bind_front`. Router accepts the resulting callable:

```cpp
class UserApi {
public:
    usub::uvent::task::Awaitable<void>
    show(usub::unet::http::RequestReader &,
         usub::unet::http::ResponseWriter &res,
         const UriParams &params) {
        // ... uses this->state_
        co_await res.send(std::string{"ok\n"});
    }
private:
    std::atomic<std::uint64_t> state_{0};
};

auto api = std::make_shared<UserApi>();
server.handle("GET", "/users/{id}",
              std::bind_front(&UserApi::show, api.get()));
```

Keep the instance alive as long as the server is running - typically a `std::shared_ptr` captured in `main`.

## Handler shape overloads

The router accepts several signatures & adapts them internally:

- `Awaitable<void>(RequestReader&, ResponseWriter&)`
- `Awaitable<void>(RequestReader&, ResponseWriter&, RadixMatch&)`
- `Awaitable<void>(RequestReader&, ResponseWriter&, RadixMatch::UriParams&)`
- `Awaitable<void>(RequestReader&, ResponseWriter&, const RadixMatch::UriParams&)`

Passing `RadixMatch&` gives access to `match.route`, `match.params`, `match.param("name")`, etc.

## Streaming responses

For long / chunked / server-sent-events bodies:

```cpp
usub::uvent::task::Awaitable<void>
tail_logs(usub::unet::http::RequestReader &,
          usub::unet::http::ResponseWriter &res) {
    res.metadata.status_code = 200;
    res.headers.addHeader("content-type", "text/plain");
    if (!co_await res.start()) co_return;
    for (;;) {
        std::string line = co_await next_log_line();
        if (line.empty()) break;
        if (!co_await res.chunk(std::move(line))) co_return;   // peer gone
    }
    co_await res.end();
}
```

`chunk(...)` returning `false` means the peer disconnected. Bail.

## Sending a file

```cpp
usub::uvent::task::Awaitable<void>
serve_file(usub::unet::http::RequestReader &,
           usub::unet::http::ResponseWriter &res) {
    int fd = ::open("./unet_demo_file.txt", O_RDONLY);
    if (fd < 0) { res.metadata.status_code = 404; co_await res.send(std::string{"missing\n"}); co_return; }

    res.metadata.status_code = 200;
    res.headers.addHeader("content-type", "text/plain");
    co_await res.file(fd, file_size_of(fd));
    ::close(fd);
}
```

On POSIX this hits `sendfile(2)` when available.

## Reading peer info

`req.peer` is a snapshot copy captured at bootstrap:

```cpp
std::cout << req.peer.ip << ":" << req.peer.port
          << " alpn=" << req.peer.alpn
          << " tls=" << req.peer.ssl << "\n";
```

Marked experimental because the shape may change. Don't build load-bearing logic on the exact fields yet.

## Upgrade handlers

Registered separately (`handleUpgrade` instead of `handle`), take a third `UpgradeContext &`:

```cpp
server.handleUpgrade("POST", "/vless",
    [auth](usub::unet::http::RequestReader &req,
           usub::unet::http::ResponseWriter &res,
           usub::unet::http::UpgradeContext &ctx)
       -> usub::uvent::task::Awaitable<void> {
        co_await usub::unet::vless::upgrade(req, res, ctx, auth);
    });
```

See [Upgrade routes](upgrade.md) for the full mechanism.

## Notes

- Don't block. Every I/O op has a `co_await` variant - use those.
- Don't capture references to `RequestReader` / `ResponseWriter` past the handler's `co_return`. They live in the session's frame.
- If you spawn detached work with `co_spawn`, capture what you need by value or via `shared_ptr`.
