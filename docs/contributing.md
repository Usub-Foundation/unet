# Contributing

## Where help lands well

- HTTP/1 & HTTP/2 parser edge cases. If you can write a failing test that hits an RFC corner we mishandle, that's gold.
- WebSocket compression (RFC 7692, `permessage-deflate`). Design + implementation.
- HTTP/2 client. Full `ClientSession<VERSION::HTTP_2_0>` from scratch.
- Client reliability - timeouts, malformed responses, TLS renegotiation edges.
- Docs that drift out of sync with headers. If you notice something on this site doesn't match `include/unet/**`, that's a bug too.

## Workflow

1. For non-trivial changes, open an issue or discussion first so the shape is agreed before code lands.
2. Small, reviewable commits. `git rebase -i` to squash noise before opening the PR.
3. Tests for anything behavior-changing. Regression coverage when fixing a bug.
4. Docs updates when public behavior changes.

## Style

- C++23. Coroutines by default for anything that touches I/O.
- Match the file you're editing. If you disagree with the local style, argue for it in a separate PR that ONLY changes style.
- Prefer clear state transitions in parsers over dense clever logic. The person debugging this in 18 months might be you.
- Comments explain *why*, not *what*. If the identifier is well-named, don't restate it.

## Testing

Tests live under `tests/`. Build with `-DUNET_BUILD_TESTS=ON` (default on):

```
cmake -S . -B build -G Ninja -DUNET_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Coverage is useful signal, not a quality gate. Adding targeted tests when fixing bugs is expected.

## Links

- Issues: https://github.com/Usub-development/unet/issues
- Discussions: https://github.com/Usub-development/unet/discussions
- Pull requests: https://github.com/Usub-development/unet/pulls
