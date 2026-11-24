---
title: "Bindings"
---

Detent's language bindings call `libdetent_reader`'s C ABI (`detent/reader.h`)
rather than reimplementing it, so evaluation, hashing, trust checks and
statuses are the same as the C++ [`Reader`](/Detent/reference/cpp-api/reader/).
Each exposes one type, `Reader`, with the same calls.

| Language | Directory | Technology | CMake option |
| --- | --- | --- | --- |
| Python | `bindings/python/` | C extension | `DETENT_BUILD_PYTHON=ON` |
| Rust | `bindings/rust/` | `extern "C"`, no build-time C++ | `DETENT_BUILD_RUST=ON` |

Both link the shared `libdetent_reader`, which exports only the `detent_*` C
symbols. `bindings/tests/scenarios.json` holds the cases the C++, Python and
Rust tests all run.

1. [Python API](/Detent/reference/bindings/python/)
2. [Rust API](/Detent/reference/bindings/rust/)

Any other language can read knobs through
[`detent get`](/Detent/reference/cli/detent/), or by calling the C ABI
directly.

:::hint[More languages]{.unimplemented}
There are no Go or Java bindings yet.
:::
