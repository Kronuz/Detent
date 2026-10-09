---
title: "Python API"
---

```python
import detent
```

The `detent` extension module is built with pybind11 when
`DETENT_BUILD_PYTHON=ON`.

---

## `detent.Client`

```python
detent.Client(
    defaults_path: str,
    source_path: str | None = None,
    cli_overrides: list[str] = [],
    poll_interval_ms: int = 200,
)
```

Constructs a client with the given baked defaults and, optionally, a file
source. Raises `detent.DetentError` for missing or invalid defaults and invalid
CLI overrides.

### `eval`

```python
Client.eval(name: str, hash_val: str | None = None, switch_val: str | None = None) -> bool
```

Evaluates a bool knob with the [standard precedence](/Detent/reference/evaluation/).

### `get`

```python
Client.get(name: str, switch_val: str | None = None) -> int
```

Reads an int knob. `switch_val` is accepted for symmetry and ignored.

### `health`

```python
Client.health() -> dict
```

```python
{'initialized': True, 'source_connected': True, 'accepted_version': 7,
 'staleness_seconds': 0.4, 'bool_knobs': 2, 'int_knobs': 1, 'cli_overrides': 0}
```

---

## `detent.DetentError`

Raised for every construction and read failure. The message starts with the
error category:

```python
try:
    client.get("search:nope")
except detent.DetentError as e:
    print(e)   # MissingKnob: search:nope (not present in current snapshot)
```

Reads are in-memory and non-blocking.

:::hint[Not yet in the Python binding]{.unimplemented}
1. The HTTP distributor source and managed generation source.
2. `read_view()`, `get_as`, and the `_or_panic` helpers.
3. A `check()` alias for `eval()`.
4. Category-specific exceptions: `KeyError` for `MissingKnob`, `ValueError`
   for `WrongType` and `InvalidName`, `SystemError` for `Uninitialized`, and
   `OverflowError` for conversions.
5. Type stubs (`.pyi`) and a published wheel.
:::
