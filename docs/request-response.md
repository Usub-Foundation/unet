# Request and Response

## Request Model

Main type: `usub::unet::http::Request`

Key fields:

- `metadata.method_token`
- `metadata.version`
- `metadata.uri` (`scheme`, `authority`, `path`, `query`, `fragment`)
- `metadata.authority`
- `headers` (`usub::unet::header::Headers`)
- `body`
- `user_data` (`std::any`)

### Query Convenience

`Request::getQueryAs<T>()` currently returns from `metadata.uri.query`.

## Response Model

Main type: `usub::unet::http::Response`

Key fields:

- `metadata.version`
- `metadata.status_code`
- `metadata.status_message` (optional override)
- `headers`
- `body`

### Fluent Helpers

```cpp
res.setStatus(200)
   .addHeader("content-type", "application/json")
   .setBody("{\"ok\":true}");
```

Notes:

- `setBody(...)` updates `content-length`
- `setBody(...)` does not automatically set `content-type`
- `setStatus(std::string)` exists but is deprecated

## Headers Container

`usub::unet::header::Headers` behavior:

- header names are normalized to lowercase
- multiple values per key are supported
- common methods: `addHeader`, `contains`, `first`, `all(key)`, `value(key)`

## HTTP Status Codes

`STATUS_CODE` enum includes common RFC and extended values (for example `OK`, `NOT_FOUND`, `INTERNAL_SERVER_ERROR`).

## Parser Limits (Globals)

- `max_headers_size` (default `256 * 1024`)
- `max_method_token_size` (default `uint8_t` max)
- `max_uri_size` (default `uint16_t` max)
