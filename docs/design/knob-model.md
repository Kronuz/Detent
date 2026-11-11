# Knob model

What a knob is, how knobs are named, grouped, overridden and rolled out, and how a read evaluates one. This is the contract every reader follows: the C++ client, the Python and Rust bindings, scoped read views and `detent get`.

Sources: `include/detent/model.hh` (types), `include/detent/validation.hh` (names), `include/detent/detail/evaluation.hh` (evaluation), `src/detent/hashing.cc` (bucket), `src/detent/json_schema.cc` and `src/detent/json_patch.cc` (formats), `src/detent/client.cc` (reads, versions, merge). Tests: `tests/detent/test_eval_semantics.cc`, `test_hash_golden.cc`, `test_model_and_schema.cc`, `test_json_patch.cc`, `test_overrides.cc`, `test_duplicate_precedence.cc`.

## Knobs

A knob is one named value a running process reads. It has one of two types, fixed by its definition:

| Type | Value | Refinements | Read with |
| --- | --- | --- | --- |
| `bool` | `true` or `false` | `rollout_pct`, `overrides` | `eval` |
| `int` | signed 64-bit integer | none | `get`, `get_as<T>` |

`KnobDef` is a closed variant of `BoolDef` and `IntDef`. There are no string, float or structured knobs. Reading a bool with `get`, or an int with `eval`, is a `WrongType` error.

A definition is one atomic unit. Publishing a knob replaces its whole definition; no field is merged.

### Names

A knob name is a **knobset** and a **local name** joined by one colon, `^[A-Za-z0-9._/-]+:[A-Za-z0-9._/-]+$`, at most 200 bytes, case-sensitive:

```text
checkout:killswitch_new_path
search:max_results
```

The knobset groups related knobs, usually by service or team. The authority, relay and agent call a knobset a **namespace**: it is the unit of atomic writes, consistent reads, subscription and revision (see [namespace-delivery.md](namespace-delivery.md)). A name with no colon, two colons, or an empty side is `InvalidName`.

### Groups

A **group key** is an opaque, case-sensitive string of 1 to 128 bytes. A bool knob's `overrides` map group keys to either a forced `bool` or a group rollout `{"rollout_pct": N}`. The caller names the group it belongs to with `switch_val`; matching is exact.

### Rollout

`rollout_pct` is an integer from 0 to 100. A rollout turns a knob on for a stable fraction of callers, chosen by a hash value (`hash_val`) the caller supplies: a user id, a host name, a tenant. A bool definition carries one knob-wide `rollout_pct` and one `rollout_pct` per group override. An unset knob-wide `rollout_pct` is null in the JSON form.

## Evaluation

### Bool: `eval(name, hash_val?, switch_val?)`

1. **CLI override.** If the process has an override for `name`, return it. An int override is `WrongType`.
2. **Lookup.** Find `name` in the current snapshot. Absent is `MissingKnob`; an int knob is `WrongType`.
3. **Group.** If `switch_val` is present and exactly matches a key in `overrides`: a forced bool is returned as is; a group rollout returns `bucket < group_pct` when `hash_val` is present and the knob's `default` when it is not.
4. **Knob-wide rollout.** If `rollout_pct` is set and `hash_val` is present, return `bucket < rollout_pct`.
5. **Default.** Return `default`.

A `switch_val` that matches no override falls through to step 4. A group match wins over the knob-wide rollout even when a hash is present. Without `hash_val` every rollout is skipped.

```text
override? -> lookup? -> group match? -- forced -----------> value
                            |      \-- group rollout ----> bucket < group_pct | default
                            no
                            v
                   rollout_pct and hash_val? -> bucket < rollout_pct | default
```

### Int: `get(name, switch_val?)`

1. **CLI override.** Return it; a bool override is `WrongType`.
2. **Lookup.** Absent is `MissingKnob`; a bool knob is `WrongType`.
3. Return `default`.

`switch_val` is accepted so `get` and `eval` share a shape, and ignored: int knobs have no group overrides. `get_as<T>` calls `get` and range-checks with `std::in_range<T>`; a value that does not fit is `ConversionFailed`, never truncated.

### Stable bucket

```text
bucket = big_endian_uint64(SHA256(name || 0x00 || hash_val)[0..8]) % 100
```

1. **Domain separation.** The zero byte keeps `("ab", "c")` and `("a", "bc")` apart.
2. **Stickiness.** The same `(name, hash_val)` gives the same bucket on every host, process and language, across restarts. SHA-256 is normative; a language runtime's default hash is randomized per process and breaks this.
3. **Independence.** The name is in the input, so one entity gets unrelated buckets for different knobs.
4. **Monotonic ramps.** `bucket < pct` means raising `pct` only adds callers. `0` enables nobody; `100` enables every caller that supplies a hash.
5. **Group-independent.** The bucket never depends on the group; a group rollout and the knob-wide rollout compare the same bucket to different thresholds.

`tests/detent/test_hash_golden.cc` pins 24 vectors generated with Python's `hashlib`; an implementation in another language reproduces them.

### Errors

Every read returns a value or a `KnobError`: `Uninitialized`, `InvalidName`, `MissingKnob`, `WrongType`, `ConversionFailed` or `SourceFailure`. Detent never invents a value for a knob it does not know. The `_or_panic` helpers abort on any error. There is no per-call fallback in the client API.

## Snapshots and versions

A **snapshot** is an immutable, validated map from knob name to definition, with a `version`, an optional `published_at` (RFC 3339 UTC) and a `source_id`. A reader sees exactly one snapshot per call; `read_view()` pins one for several reads. The updater builds a new snapshot for every accepted update and publishes it with one atomic swap, so no reader sees a partial update.

**Baked defaults** are a snapshot file shipped with the binary, loaded at startup as version `0`. They introduce knob names and remain the value of any knob the live data does not mention.

**Merge.** An accepted live snapshot overlays the defaults: a knob in the live snapshot replaces the whole definition from the defaults; a knob absent from it keeps its default definition.

**Version rule.** A live snapshot or patch carries a positive integer `version`, and a client accepts it only when it is greater than the last accepted one. An older, repeated or replayed update is counted as stale and ignored. A source that fails to parse or validate leaves the last good snapshot in place and counts a refresh failure.

**CLI overrides** are `name=value` pairs given to one process at startup. `true` and `false` (lowercase) parse as bool; anything else parses as a signed base-10 `int64`. They shadow the defaults and every later snapshot for that process, are immutable after startup, and a duplicate name is a startup error.

### Patches

A `SnapshotPatch` carries only the knobs that changed: a definition upserts a knob; `null` removes it from the live overlay, so it falls back to its baked default. `diff_snapshots` computes the smallest patch between two snapshots, `apply_patch` applies one (rejecting a result over `max_knobs`), and a client applies a patch onto its own last accepted live state, never onto a base the source names.

## JSON format

A source snapshot, and a baked defaults file without `version` and `published_at`:

```json
{
  "schema_version": 1,
  "version": 7,
  "published_at": "2026-11-11T09:30:00Z",
  "knobs": {
    "checkout:killswitch_new_path": {
      "type": "bool", "default": false, "rollout_pct": 10,
      "overrides": { "employees": true, "beta": { "rollout_pct": 50 } }
    },
    "search:max_results": { "type": "int", "default": 25 }
  }
}
```

Parsing is strict and rejects the whole input on any violation:

1. `schema_version` is exactly `1`. `version` is required in a source snapshot and forbidden in baked defaults; `published_at` likewise.
2. A top-level field that is unknown, repeated, or missing (`schema_version`, `knobs`) is a schema error.
3. A knob has `type` and `default`. An unknown field, a missing `default`, a default of the wrong type, or `overrides` and `rollout_pct` on an int knob is rejected.
4. `rollout_pct` is an integer in 0 to 100 or `null`. An override is a bool or an object whose only member is `rollout_pct` in 0 to 100.
5. A repeated knob name, or a repeated override key within a knob, is rejected; the first duplicate in input order is the one reported, and it wins over errors that come after its second occurrence.
6. Limits: at most 100,000 knobs (`kDefaultMaxKnobs`), at most 16 MiB, bracket nesting at most 64 (checked before parsing), no NUL byte.

Failures carry an `UpdateRejectReason`: `Io`, `Transport`, `Parse`, `Schema`, `Version`, `Oversize` or `Checksum`. `serialize_snapshot` is the exact inverse of `parse_source_snapshot`.
