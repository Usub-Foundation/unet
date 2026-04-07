# Multipart Form Data

Multipart parser utility lives in:

- `unet/mime/multipart/form_data/generic.hpp`

Main type: `usub::unet::mime::multipart::FormData`

## Basic Usage

```cpp
using usub::unet::mime::multipart::FormData;

std::string boundary = "----MyBoundary";
std::string body = /* raw multipart payload */;

FormData form(boundary);
auto result = form.parse(body);
if (!result) {
    // result.error() has an error string
}

if (form.contains("file")) {
    const auto& parts = form.at("file");
    // parts[i].data, parts[i].content_type, parts[i].disposition
}
```

## Parsed Part Structure

Each `Part` includes:

- `content_type`
- `disposition` map
- `data`
- extra `headers` map

Parts are indexed by `name` from `Content-Disposition`.

## Notes

- boundary must be provided explicitly
- parsing is whole-buffer (not streaming)
- error type is `std::expected<void, std::string>`
