# Roadmap

Grouped by rough distance, not deadlines. Items move around based on what's actually blocking users.

## Near-term

- **h2c decision.** RFC 9113 formally deprecated the `Upgrade: h2c` dance. Likely path: cut the Upgrade handler, keep prior-knowledge sniff behind a config flag, drop the seeded-stream-1 machinery. Retires the "pre-buffered body" follow-up.
- **Response-phase middleware invocation on h1.** Declared, not wired. Needs a hook in `v1::ResponseSerializer`'s send path.
- **Body-trickle timeout on h1.** Header phase already has a Slowloris guard via `idle_header_timeout`. Body phase has none; add a min-throughput window over the body-read loop.
- **h2 SETTINGS extension gap.** IDs 0x07-0x0a are silently dropped instead of dispatched to user handlers or applied to `remote_`. `NO_RFC7540_PRIORITIES` (0x09) & `ENABLE_CONNECT_PROTOCOL` (0x08) should update `remote_` at least.
- **`ResponseWriter::sendFrame` on h2.** For handlers that want to emit custom frame types (per an extension advertised via SETTINGS). Currently only the frame-handler receive-side is exposed.

## Mid-term

- **HTTP/2 client** (`ClientSession<VERSION::HTTP_2_0>`) + ALPN plumbing on the client side.
- **WebSocket `permessage-deflate`** (RFC 7692). The parser currently rejects any non-zero RSV bit; loosen to "RSV1 allowed iff deflate was negotiated on this connection."
- **gRPC support.** Trailer emission on h1 & h2, `Trailers-Only` handshake shape, unary handler ergonomics.
- **Real client-side session for `RequestWriter`.** The type & `send(Request)` exist as scaffolding; nothing binds ops to it. Either build the client-side session or delete the stub.
- **SNI-aware multi-vhost hosting on one port.** Match on the SNI hostname before dispatching to a router set.
- **CMake polish & splittable targets.** parser-only, protocol-only, server, full.

## Long-term

- **HTTP/3 / QUIC.** Depends on a QUIC runtime we don't yet have.
- **Benchmark & profiling docs.** With numbers, not adjectives.
- **Stats on `v1::Connection` / `v2::Connection`.** Streams opened / closed, bytes moved, requests served counter. Currently `requests_served` exists on h1 only.

## Out of scope this cycle

- Non-HTTP experimental modules under `include/` that aren't part of the active runtime.
- Anything that would require a rewrite of `uvent`.
