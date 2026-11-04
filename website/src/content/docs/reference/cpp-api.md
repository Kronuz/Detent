---
title: "C++ API"
---

The C++ library is the reference implementation every other reader wraps. It
targets C++20 and lives in the `detent` namespace.

---

## Targets and Headers

| CMake target | Header | What it provides |
| --- | --- | --- |
| `detent::reader` | `detent/reader.hh` | `Reader`, `Lookup`, `ReaderError` |
| | `detent/reader.h` | The C ABI under them: `detent_open`, `detent_eval`, `detent_get`, `detent_read`, `detent_refresh`, `detent_revision`, `detent_close` |
| `detent::detent` | `detent/model.hh` | `Snapshot`, `KnobDef`, `BoolDef`, `IntDef`, `GroupRollout`, `SnapshotPatch` |
| | `detent/json_schema.hh` | `parse_defaults()`, `parse_source_snapshot()` and the other knob-definition parsers |
| | `detent/result.hh` | `Result<T, E>` |
| | `detent/errors.hh` | `KnobError`, `ErrorCategory`, `UpdateError`, `UpdateRejectReason` |
| | `detent/validation.hh` | `is_valid_knob_name()`, `is_valid_group_key()` |

Applications read knobs with `detent::reader`. `detent::detent` is the knob
model and its JSON parsers, for tools that build or check definitions; neither
target has a networking dependency.

---

## Reference Pages

1. [Reader](/Detent/reference/cpp-api/reader/): opening a store, lookups with
   per-call fallbacks, statuses, refresh and revisions.

---

## Conventions

1. **Lookups never fail loudly.** Every lookup returns the caller's fallback on
   any error; `lookup()` also returns the status.
2. **Opening is the only call that throws** (`detent::ReaderError`).
3. **Optional arguments are `std::optional<std::string_view>`.** Pass nothing
   to omit a hash value or switch value.
4. **The reader starts no thread.** A lookup notices a new publish by itself;
   `refresh()` attaches it earlier from a thread you choose.
