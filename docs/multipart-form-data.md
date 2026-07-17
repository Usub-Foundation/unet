# Multipart form data

Whole-buffer parser for `multipart/form-data` bodies. Lives in `unet/mime/multipart/form_data/generic.hpp` as `usub::unet::mime::multipart::FormData`.

Not streaming. Read the whole request body first (`req.collect(limit)`), then hand it to the parser.

## Usage

```cpp
#include <unet/mime/multipart/form_data/generic.hpp>

using usub::unet::mime::multipart::FormData;

usub::uvent::task::Awaitable<void>
upload(usub::unet::http::RequestReader &req,
       usub::unet::http::ResponseWriter &res) {
    auto body = co_await req.collect(16 * 1024 * 1024);
    if (!body) { res.metadata.status_code = 413; co_await res.send(std::string{"too big\n"}); co_return; }

    std::string boundary = extract_boundary(req);   // parse Content-Type header
    FormData form(boundary);
    auto ok = form.parse(*body);
    if (!ok) { res.metadata.status_code = 400; co_await res.send(*ok.error()); co_return; }

    if (form.contains("file")) {
        const auto &parts = form.at("file");
        for (const auto &p : parts) {
            // p.data, p.content_type, p.disposition, p.headers
        }
    }

    res.metadata.status_code = 200;
    co_await res.send(std::string{"ok\n"});
}
```

## Part shape

Each `Part`:

- `content_type` (`std::string`).
- `disposition` (`std::unordered_map<std::string, std::string>` - the `Content-Disposition` parameters).
- `data` (`std::string` - raw part body).
- `headers` (`std::unordered_map<std::string, std::string>` - anything else in the part's header block).

Parts are indexed by the `name` parameter of `Content-Disposition`.

## API

- `FormData(std::string boundary)` - construct with the boundary from `Content-Type`.
- `parse(std::string_view body) -> std::expected<void, std::string>` - run the parser.
- `contains(std::string_view name)` - does at least one part exist with this name?
- `at(std::string_view name) -> const std::vector<Part> &` - all parts with this name.

## Notes

- Boundary must be extracted from the request `Content-Type` header (`multipart/form-data; boundary=...`) before construction. Quoted-boundary support is on the caller side.
- Parsing is whole-buffer. Files larger than what you're willing to collect need a streaming multipart parser, which we don't ship yet.
- Error is `std::string` inside the `std::expected` failure branch - not a typed error.
