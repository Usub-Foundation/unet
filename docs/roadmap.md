# Roadmap

## Near-Term

- tighten HTTP/1 parser edge-case coverage
- improve middleware phase consistency (including response phase execution)
- simplify server configuration ergonomics
- improve test determinism and quality
- per-stream tunneling on HTTP/2 (plain CONNECT, Extended CONNECT for
  WebSockets-over-h2 per RFC 8441)
- gRPC support: trailers in `v1::ResponseSerializer` and the H2 frame loop
- WebSocket permessage-deflate (RFC 7692): negotiate `permessage-deflate`
  in the upgrade handshake, set/honour RSV1 on the first frame of a
  compressed message, run payloads through DEFLATE. Today the parser
  rejects any non-zero RSV bit with `RESERVED_BITS_SET`; that check
  needs to loosen to "RSV1 allowed iff deflate was negotiated for this
  connection."

## Mid-Term

- HTTP/2 client (`ClientSession<VERSION::HTTP_2_0>`)
- stronger client coverage (timeouts, malformed responses, TLS edges)
- clearer public API docs around route constraints and handler overloads
- packaging and CMake polish, splittable library targets (parser-only,
  protocol-only, server, full)
- SNI-aware multi-vhost hosting on one port

## Long-Term

- HTTP/3 / QUIC
- benchmark and profiling documentation

## Out Of Scope In This Cycle

- expanding docs for non-HTTP experimental modules
