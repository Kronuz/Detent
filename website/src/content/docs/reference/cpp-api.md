---
title: "C++ API"
---

The C++ library is the reference implementation every other reader wraps. It
targets C++20 and lives in the `detent` namespace.

---

## Targets and Headers

| CMake target | Header | What it provides |
| --- | --- | --- |
| `detent::detent` | `detent/client.hh` | `Client`, `InitOptions`, `HealthSnapshot`, `Metrics` |
| | `detent/model.hh` | `Snapshot`, `KnobDef`, `BoolDef`, `IntDef`, `GroupRollout`, `SnapshotPatch` |
| | `detent/result.hh` | `Result<T, E>` |
| | `detent/errors.hh` | `KnobError`, `ErrorCategory`, `UpdateError`, `UpdateRejectReason` |
| | `detent/read_view.hh` | `ClientReadView`, `ReadSource`, `EvaluationView` |
| | `detent/global.hh` | The process-wide `detent::global` adapter |
| | `detent/testing.hh` | `TestKnobs`, `TestGuard` |
| | `detent/overrides.hh` | `OverrideTable`, `parse_cli_overrides()` |
| | `detent/validation.hh` | `is_valid_knob_name()`, `is_valid_group_key()` |
| `detent_http::detent_http` | `detent_http/http_source.hh` | `make_http_poll_source()` |
| `detent::local_client` | `detent_local/client_source.hh` | `make_local_read_source()` for managed generations |

`detent::detent` has no networking dependency. Only link `detent_http` or
`detent::local_client` when you use those sources.

---

## Reference Pages

1. [Client](/Detent/reference/cpp-api/client/): construction, reads, views,
   health and metrics.
2. [Sources](/Detent/reference/cpp-api/sources/): file, HTTP, generic and
   managed-generation sources.
3. [Global Access and Testing](/Detent/reference/cpp-api/global-and-testing/):
   the `detent::global` adapter, `TestKnobs`, and CLI overrides.

---

## Conventions

1. **Errors are values on the read path.** Reads return `Result<T, KnobError>`;
   only the `_or_panic` helpers abort.
2. **Errors are exceptions at startup.** The `Client` constructor throws on
   invalid defaults, invalid CLI overrides, or invalid options.
3. **Optional arguments are `std::optional<std::string_view>`.** Pass
   `std::nullopt` (or nothing) to omit a hash value or switch value.
4. **Nothing on the read path allocates, locks or does I/O.**

`Result<T, E>` is a small `std::expected`-shaped type, since Detent targets
C++20:

```cpp
auto r = client.get("search:max_results");
if (r) {                         // or r.has_value()
    std::int64_t v = r.value();
} else {
    const detent::KnobError& e = r.error();
    std::string message = e.what();
}
```
