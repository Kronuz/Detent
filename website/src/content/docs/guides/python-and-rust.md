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

reader.get_bool("checkout:killswitch_new_path", False)
reader.get_bool("payments:new_processor", False, hash="user-42")
reader.get_bool("checkout:new_flow", False, switch_value="beta_testers")
reader.get("search:max_results", 25)
```

`get` returns the knob's integer. `get_bool` returns `False` for 0 and `True`
for 1, and its fallback for any other value. Every lookup returns its fallback
on any error. `lookup()` says why:

```python
result = reader.lookup("search:nope", 0)
result.status   # <Status.MISSING: 1>
result.value    # 0, the fallback
```

A failed open raises `detent.ReaderError`, with the reason in `.status`. See the
[Python API reference](/Detent/reference/bindings/python/).

---

## Rust

```rust
let reader = detent::Reader::open("agent-state", agent_uid, &[], 0)?;

let on = reader.get_as("payments:new_processor", false, Some("user-42"), None);
let beta = reader.get_as("checkout:new_flow", false, None, Some("beta_testers"));
let max = reader.get("search:max_results", 25, None, None);

match reader.lookup("search:nope", 0, None, None) {
    Ok(value) => println!("{value}"),
    Err(status) => println!("fallback: {}", status.name()),
}
```

`get` returns an `i64` and `get_as` the type of its fallback (`bool` or a
fixed-width integer), each its fallback on any error; `get_as` also falls back
for a value outside that type (for `bool`, other than 0 or 1). `lookup` returns
`Result<i64, Status>`. See the
[Rust API reference](/Detent/reference/bindings/rust/).

---

## When a Publish Can't Be Read

Each namespace follows its own publishes. When a namespace's new publish can't
be read (its files damaged or untrusted, or past the reader's mapping cap), that
namespace keeps answering its last good publish with status `ok`, and the
reader's other namespaces move on. Only `refresh()` reports it, with the
failure's status; the namespace tries again at its next publish or `refresh()`.
A process that must know calls `refresh()` from a thread of its own.

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
