---
title: "Python and Rust"
---

The Python and Rust bindings wrap the same C++ `detent::Client`, so reads have
the same semantics, the same stable rollout buckets and the same fail-fast
errors as C++.

---

## Building the Bindings

Both are off by default:

```sh
cmake -B build -DDETENT_BUILD_PYTHON=ON -DDETENT_BUILD_RUST=ON
cmake --build build -j
```

The Python module needs the Python 3 development headers (it's built with
pybind11). The Rust crate needs `cargo` and `rustc` (it's built with `cxx`).

---

## Python

```python
import detent

client = detent.Client(
    "/etc/myapp/just_knobs.json",
    source_path="/var/lib/myapp/live.json",
    cli_overrides=["search:max_results=999"],
    poll_interval_ms=200,
)

client.eval("payments:new_processor", hash_val="user-42")
client.eval("checkout:new_flow", switch_val="beta_testers")
client.get("search:max_results")
client.health()
# {'initialized': True, 'source_connected': True, 'accepted_version': 7,
#  'staleness_seconds': 0.4, 'bool_knobs': 2, 'int_knobs': 1, 'cli_overrides': 1}
```

Every failure, from a bad defaults file at construction to a missing or
wrong-typed knob on read, raises `detent.DetentError`:

```python
try:
    client.get("search:nope")
except detent.DetentError as e:
    print(e)   # MissingKnob: search:nope ...
```

See the [Python API reference](/Detent/reference/bindings/python/).

---

## Rust

```rust
let client = detent::Client::new(
    "/etc/myapp/just_knobs.json",
    Some("/var/lib/myapp/live.json"),
    vec![],
    200,
)?;

let on = client.eval("payments:new_processor", Some("user-42"), None)?;
let beta = client.eval("checkout:new_flow", None, Some("beta_testers"))?;
let max = client.get("search:max_results", None)?;
let health = client.health();
```

Reads return `Result<T, detent::DetentError>`. See the
[Rust API reference](/Detent/reference/bindings/rust/).

---

## What the Bindings Don't Do Yet

:::hint[Binding gaps]{.unimplemented}
The bindings expose the core client with a file source only. Not yet available
from Python or Rust:

1. The HTTP distributor source. Use a file source, or read through
   [`detent get`](/Detent/guides/shell-and-agent/) and the host agent.
2. Pinned read views (`read_view()`) and the managed generation source.
3. Checked integer conversion (`get_as`) and the `_or_panic` helpers.
4. The Python `check()` alias and the per-error-category exception mapping
   (`KeyError` for a missing knob, `ValueError` for a wrong type or invalid
   name, `OverflowError` for a failed conversion) that the original
   specification describes.
5. Packaging: there is no wheel on PyPI and no crate on crates.io.
:::
