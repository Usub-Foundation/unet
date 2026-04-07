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

## Binding Class Member Handlers

You can register a class member function by binding an instance first.

```cpp
class AdminController {
public:
    usub::uvent::task::Awaitable<void>
    status(usub::unet::http::Request& req, usub::unet::http::Response& res) {
        (void)req;
        res.setStatus(200).setBody("admin ok\n");
        co_return;
    }
};

int main() {
    usub::Uvent runtime{2};
    usub::unet::http::ServerRadix server{runtime};

    AdminController controller{};
    server.handle("GET", "/admin/status",
                  std::bind_front(&AdminController::status, &controller));

    runtime.run();
}
```

For member handlers with URI params, use the same pattern:
`std::bind_front(&Controller::method, &controller_instance)`.

## Practical Rules

- keep handlers non-blocking
- set status/headers/body explicitly
- use middleware for cross-cutting concerns
