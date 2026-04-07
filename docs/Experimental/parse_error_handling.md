# Experimental Parse Error Handling Notes

This page summarizes practical parse-error handling for current HTTP/1 parser APIs.

## What You Receive

On parser failure (`RequestParser` / `ResponseParser`), you receive `ParseError` with:

- `expected_status`: suggested HTTP status for malformed input
- `message`: parser failure reason
- `tail`: reserved buffer (currently lightly used)

## Practical Server-Side Strategy

1. Set `response.metadata.status_code` from `expected_status`.
2. Log parser message and relevant request metadata.
3. Treat partially parsed requests as invalid and stop processing.

## Practical Client-Side Strategy

When `ClientImpl::request(...)` returns `PARSE_FAILED`:

1. Log `ClientError.message`.
2. If present, log `ClientError.parse_error->message`.
3. Treat the response as unusable and retry only when method semantics are safe.

## Operational Advice

- track parse error rates by peer/service
- alert on sustained increases
- keep raw byte capture behind debug flags and privacy controls
