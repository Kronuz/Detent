# Knob model

What a knob is, how knobs are named, grouped, overridden and rolled out, and how a read evaluates one. The wire, page and image encodings of a definition are in [one-knob-type.md](one-knob-type.md). This is the contract every reader follows: the C++ client, the Python and Rust bindings, scoped read views and `detent get`.

Sources: `include/detent/model.hh` (types), `include/detent/validation.hh` (names), `include/detent/detail/evaluation.hh` (evaluation), `src/detent/hashing.cc` (bucket), `src/detent/json_schema.cc` and `src/detent/json_patch.cc` (formats), `src/detent/client.cc` (reads, versions, merge). Tests: `tests/detent/test_eval_semantics.cc`, `test_hash_golden.cc`, `test_model_and_schema.cc`, `test_json_patch.cc`, `test_overrides.cc`, `test_duplicate_precedence.cc`, `test_one_knob_differential.cc`.

## Knobs

A knob is one named signed 64-bit integer a running process reads. Its definition has four fields:

| Field | Value | Meaning |
| --- | --- | --- |
| `default` | `int64` | The value when nothing else applies |
| `overrides` | group key to a forced `int64` or `{"rollout_pct": N}` | Per-group values |
| `rollout_pct` | 0 to 100, or absent | The knob-wide percentage of hashed callers served `rollout_value` |
| `rollout_value` | `int64`, or absent | The value a caller inside a percentage gets |

`rollout_value` is present exactly when the definition has a percentage, knob-wide or in a group; `definition_error()` in `include/detent/model.hh` enforces it everywhere a definition is built or decoded. A flag is a knob with `default` 0 and `rollout_value` 1; nothing marks it as a flag. There are no string, float or structured knobs.

A definition is one atomic unit. Publishing a knob replaces its whole definition; no field is merged.

### Names

A knob name is a **knobset** and a **local name** joined by one colon, `^[A-Za-z0-9._/-]+:[A-Za-z0-9._/-]+$`, at most 200 bytes, case-sensitive:

```text
checkout:killswitch_new_path
search:max_results
```

The knobset groups related knobs, usually by service or team. The authority, relay and agent call a knobset a **namespace**: it is the unit of atomic writes, consistent reads, subscription and revision (see [namespace-delivery.md](namespace-delivery.md)). A name with no colon, two colons, or an empty side is `InvalidName`.

### Groups

A **group key** is an opaque, case-sensitive string of 1 to 128 bytes. `overrides` map group keys to a forced value or a group percentage `{"rollout_pct": N}`. The caller names its group with `switch_val`; matching is exact.

### Rollout

A percentage serves `rollout_value` to a stable fraction of callers, chosen by a hash value (`hash_val`) the caller supplies: a user id, a host name, a tenant. A caller outside that fraction, and a caller with no hash, gets `default`. Ramping a flag is raising `rollout_pct` from 0 to 100 with `default` 0 and `rollout_value` 1; ramping an integer from 25 to 50 is the same with `default` 25 and `rollout_value` 50.

## Evaluation

### `get(name, hash_val?, switch_val?)`

1. **CLI override.** If the process has an override for `name`, return it.
2. **Lookup.** Find `name` in the current snapshot. Absent is `MissingKnob`.
3. **Group.** If `switch_val` is present and exactly matches a key in `overrides`: a forced value is returned as is; a group percentage returns `rollout_value` when `hash_val` is present and `bucket < group_pct`, and `default` otherwise.
4. **Knob-wide rollout.** If `rollout_pct` is set, return `rollout_value` when `hash_val` is present and `bucket < rollout_pct`, and `default` otherwise.
5. **Default.** Return `default`.

A `switch_val` that matches no override falls through to step 4. A group match wins over the knob-wide rollout even when a hash is present.

```text
override? -> lookup? -> group match? -- forced -------------> value
                            |      \-- group percentage --> hash and bucket < group_pct ? rollout_value : default
                            no
                            v
                   rollout_pct? -> hash and bucket < rollout_pct ? rollout_value : default
                            |
                            no -> default
```

The in-bucket test lives in one function, `detent::detail::ramp()` in `include/detent/detail/evaluation.hh`; the snapshot evaluator and the paged and flat evaluators (`src/detent_local/evaluation.hh`) both call it (see [../mechanisms/evaluation.md](../mechanisms/evaluation.md)).

### `get_as<T>(name, hash_val?, switch_val?)`

`get_as<T>` calls `get` and converts: `bool` accepts exactly 0 and 1; an integer type accepts what `std::in_range<T>` accepts. Anything else is `ConversionFailed` and counts `reads_conversion_failed`; a value is never truncated.

### Stable bucket

```text
bucket = big_endian_uint64(SHA256(name || 0x00 || hash_val)[0..8]) % 100
```

1. **Domain separation.** The zero byte keeps `("ab", "c")` and `("a", "bc")` apart.
2. **Stickiness.** The same `(name, hash_val)` gives the same bucket on every host, process and language, across restarts. SHA-256 is normative; a language runtime's default hash is randomized per process and breaks this.
3. **Independence.** The name is in the input, so one entity gets unrelated buckets for different knobs.
4. **Monotonic ramps.** `bucket < pct` means raising `pct` only adds callers. `0` serves `rollout_value` to nobody; `100` serves it to every caller that supplies a hash.
5. **Group-independent.** The bucket never depends on the group; a group percentage and the knob-wide rollout compare the same bucket to different thresholds.

`tests/detent/test_hash_golden.cc` pins 24 vectors generated with Python's `hashlib`; an implementation in another language reproduces them.

### Errors

Every read returns a value or a `KnobError`: `Uninitialized`, `InvalidName`, `MissingKnob`, `ConversionFailed` or `SourceFailure`. Detent never invents a value for a knob it does not know. The `_or_panic` helpers abort on any error. There is no per-call fallback in the client API.

## Snapshots and versions

A **snapshot** is an immutable, validated map from knob name to definition, with a `version`, an optional `published_at` (RFC 3339 UTC) and a `source_id`. A reader sees exactly one snapshot per call; `read_view()` pins one for several reads. The updater builds a new snapshot for every accepted update and publishes it with one atomic swap, so no reader sees a partial update.

**Baked defaults** are a snapshot file shipped with the binary, loaded at startup as version `0`. They introduce knob names and remain the value of any knob the live data does not mention.

**Merge.** An accepted live snapshot overlays the defaults: a knob in the live snapshot replaces the whole definition from the defaults; a knob absent from it keeps its default definition.

**Version rule.** A live snapshot or patch carries a positive integer `version`, and a client accepts it only when it is greater than the last accepted one. An older, repeated or replayed update is counted as stale and ignored. A source that fails to parse or validate leaves the last good snapshot in place and counts a refresh failure.

**CLI overrides** are `name=value` pairs given to one process at startup. A value is a signed base-10 `int64`; `true` and `false` (lowercase) parse as 1 and 0. They shadow the defaults and every later snapshot for that process, are immutable after startup, and a duplicate name is a startup error.

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
      "default": 0,
      "overrides": { "employees": 1, "beta": { "rollout_pct": 50 } },
      "rollout_pct": 10, "rollout_value": 1
    },
    "search:max_results": { "default": 25 },
    "search:batch_size": { "default": 25, "rollout_pct": 10, "rollout_value": 50 }
  }
}
```

Parsing is strict and rejects the whole input on any violation:

1. `schema_version` is exactly `1`. `version` is required in a source snapshot and forbidden in baked defaults; `published_at` likewise.
2. A top-level field that is unknown, repeated, or missing (`schema_version`, `knobs`) is a schema error.
3. A knob has an integer `default`. An unknown field (`type` included), a missing `default`, or a value that is not a JSON integer in `int64` (`true` and `false` included) is rejected.
4. `rollout_pct` is an integer in 0 to 100 or `null`; `rollout_value` is an `int64` or `null`. An override is an `int64` or an object whose only member is `rollout_pct` in 0 to 100. `rollout_value` is required when the definition has any percentage and rejected when it has none.
5. A repeated knob name, or a repeated override key within a knob, is rejected; the first duplicate in input order is the one reported, and it wins over errors that come after its second occurrence.
6. Limits: at most 100,000 knobs (`kDefaultMaxKnobs`), at most 16 MiB, bracket nesting at most 64 (checked before parsing), no NUL byte.

Failures carry an `UpdateRejectReason`: `Io`, `Transport`, `Parse`, `Schema`, `Version`, `Oversize` or `Checksum`. `serialize_snapshot` is the exact inverse of `parse_source_snapshot`: it writes `default`, then `overrides` when nonempty, `rollout_pct` and `rollout_value` when set.
