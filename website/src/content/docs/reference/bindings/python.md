---
title: "Python API"
---

```python
import detent
```

The `detent` extension module is a C extension over `libdetent_reader`, built
when `DETENT_BUILD_PYTHON=ON`. It declares `Py_MOD_GIL_NOT_USED`, so it runs
on free-threaded builds without a lock.

---

## `detent.Reader`

```python
detent.Reader(store: str | bytes | os.PathLike, publisher_uid: int, namespaces=())
```

Opens the agent's store directory. `publisher_uid` is the uid the agent runs
as; `namespaces` is an iterable of `str` that limits the reader, and empty
means all of them. A store that doesn't exist yet opens, and lookups return the
fallback with `Status.UNAVAILABLE`.

Opening is the only call that raises: `detent.ReaderError` for a failed open,
`TypeError` or `OverflowError` for a bad argument. Importing the module raises
`ImportError` when the library's ABI version differs from the extension's.

There is no `close()`: the handle closes when the object is deallocated, and
every lookup holds a reference to it, so a close can never overlap a lookup.
The type can't be subclassed.

### Lookups

```python
Reader.eval(name: str, fallback: bool, hash: str | None = None, switch_value: str | None = None) -> bool
Reader.get(name: str, fallback: int) -> int
Reader.lookup(name: str, hash: str | None = None, switch_value: str | None = None) -> Lookup
```

`eval` and `get` return `fallback` on any error. A non-`bool` fallback to
`eval`, a non-`int` or `bool` fallback to `get`, or a non-`str` name raises
`TypeError`; an integer outside int64 raises `OverflowError`. Lookups keep the
GIL.

### Freshness

```python
Reader.refresh() -> Status
Reader.revision(namespace: str) -> int | None
```

`refresh()` attaches a new publish now rather than on the next lookup, and
releases the GIL while it works. `revision` returns the namespace's attached
revision, or `None`.

---

## `detent.Lookup`

`lookup()` returns a `Lookup` with `.status` (a `Status`) and `.value` (a
`bool` or `int`, or `None` unless the status is `OK`). It's truthy when the
status is `OK`.

```python
result = reader.lookup("search:max_results")
if not result:
    print(result.status.name)   # e.g. MISSING, UNAVAILABLE
```

---

## `detent.Status`

An `IntEnum` with the C ABI's values: `OK`, `MISSING`, `WRONG_TYPE`,
`INVALID_NAME`, `NOT_OPENED`, `SOURCE_FAILURE`, `UNAVAILABLE`, `UNTRUSTED`,
`INVALID_ARGUMENT`, `NO_MEMORY`, `IO`. A value newer than the binding maps to a
member named `UNKNOWN_<n>` that keeps the integer. The
[Reader reference](/Detent/reference/cpp-api/reader/#lookups) says what each
one means.

---

## `detent.ReaderError`

```python
try:
    reader = detent.Reader(store, uid)
except detent.ReaderError as e:
    print(e.status.name, e)   # UNTRUSTED detent_open: untrusted
```

:::hint[Not yet in the Python binding]{.unimplemented}
1. Type stubs (`.pyi`).
2. A published wheel.
:::
