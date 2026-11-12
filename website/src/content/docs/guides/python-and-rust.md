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
    "/etc/myapp/defaults.json",
    source_path="/var/lib/myapp/live.json",
    cli_overrides=["search:max_results=999"],
    poll_interval_ms=200,
)

client.get_bool("payments:new_processor", hash_val="user-42")
client.get_bool("checkout:new_flow", switch_val="beta_testers")
client.get("search:max_results")
client.health()
# {'initialized': True, 'source_connected': True, 'accepted_version': 7,
#  'staleness_seconds': 0.4, 'knobs': 3, 'cli_overrides': 1}
```

`get` returns the knob's integer. `get_bool` returns `False` for 0 and `True`
for 1. Every failure, from a bad defaults file at construction to a missing
knob or a `get_bool` of any other value, raises `detent.DetentError`:

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
    "/etc/myapp/defaults.json",
    Some("/var/lib/myapp/live.json"),
    vec![],
    200,
)?;

let on = client.get_bool("payments:new_processor", Some("user-42"), None)?;
let beta = client.get_bool("checkout:new_flow", None, Some("beta_testers"))?;
let max = client.get("search:max_results", None, None)?;
let health = client.health();
```

`get` returns `Result<i64, detent::DetentError>` and `get_bool` returns
`Result<bool, detent::DetentError>`, failing on any value other than 0 or 1. See the
[Rust API reference](/Detent/reference/bindings/rust/).

---

## What the Bindings Don't Do Yet

:::hint[Binding gaps]{.unimplemented}
The bindings expose the core client with a file source only. Not yet available
from Python or Rust:

1. Pinned read views (`read_view()`) and the managed generation source. Read
   through [`detent get`](/Detent/guides/shell-and-agent/) and the host agent
   for managed delivery.
2. Checked conversions other than `get_bool`, and the `_or_panic` helpers.
3. The Python `check()` alias and the per-error-category exception mapping
   (`KeyError` for a missing knob, `ValueError` for an invalid name,
   `OverflowError` for a failed conversion) that the original specification
   describes.
4. Packaging: there is no wheel on PyPI and no crate on crates.io.
:::
