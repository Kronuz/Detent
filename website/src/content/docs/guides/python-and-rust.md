---
title: "Python and Rust"
---

The Python and Rust bindings call the same reader core as C++'s
`detent::Reader`, so lookups have the same fallbacks, the same stable rollout
buckets and the same statuses in all three languages.

---

## Building the Bindings

Both are off by default:

```sh
cmake -B build -DDETENT_BUILD_PYTHON=ON -DDETENT_BUILD_RUST=ON
cmake --build build -j
```

The Python module needs the Python 3 development headers; it lands in
`build/bindings/python/`. The Rust crate needs `cargo` and `rustc`, and links
the shared `libdetent_reader` from the same build directory.

---

## Python

```python
import detent

reader = detent.Reader("agent-state", agent_uid)

reader.eval("checkout:killswitch_new_path", False)
reader.eval("payments:new_processor", False, hash="user-42")
reader.eval("checkout:new_flow", False, switch_value="beta_testers")
reader.get("search:max_results", 25)
```

Every lookup returns its fallback on any error. `lookup()` says why:

```python
result = reader.lookup("search:nope")
result.status   # <Status.MISSING: 1>
result.value    # None
```

A failed open raises `detent.ReaderError`, with the reason in `.status`. See the
[Python API reference](/Detent/reference/bindings/python/).

---

## Rust

```rust
let reader = detent::Reader::open("agent-state", agent_uid, &[])?;

let on = reader.eval("payments:new_processor", false, Some("user-42"), None);
let beta = reader.eval("checkout:new_flow", false, None, Some("beta_testers"));
let max = reader.get("search:max_results", 25);

match reader.lookup("search:nope", None, None) {
    Ok(value) => println!("{value:?}"),
    Err(status) => println!("fallback: {}", status.name()),
}
```

See the [Rust API reference](/Detent/reference/bindings/rust/).

---

## Testing

The bindings' tests publish real generations with the test writer's C face
(`detent_testing/generation_writer.h`) and run the shared cases in
`bindings/tests/scenarios.json`. [Testing](/Detent/guides/testing/) covers the
writer.

:::hint[Binding gaps]{.unimplemented}
1. Python type stubs (`.pyi`).
2. Packaging: there is no wheel on PyPI and no crate on crates.io.
:::
