---
title: "Bindings"
---

Detent's language bindings wrap the C++ `detent::Client` rather than
reimplementing it, so evaluation, hashing, validation and error categories are
identical everywhere.

| Language | Directory | Technology | CMake option |
| --- | --- | --- | --- |
| Python | `bindings/python/` | pybind11 | `DETENT_BUILD_PYTHON=ON` |
| Rust | `bindings/rust/` | `cxx` | `DETENT_BUILD_RUST=ON` |

1. [Python API](/Detent/reference/bindings/python/)
2. [Rust API](/Detent/reference/bindings/rust/)

Any other language can read knobs today through
[`detent get`](/Detent/reference/cli/detent/) and the host agent, or by
implementing the [evaluation rules](/Detent/reference/evaluation/) over a
snapshot it fetches itself.

:::hint[More languages]{.unimplemented}
There are no Go or Java bindings yet, and no direct reader for the agent's
shared-memory layout outside C++. The shared-memory format is an internal,
versioned layout; a documented, stable reader ABI would let other languages map
it directly.
:::
