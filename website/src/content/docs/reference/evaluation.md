---
title: "Evaluation Semantics"
---

These are the exact rules every Detent reader follows: the C++ client, the
Python and Rust bindings, scoped read views, and `detent get`.

---

## Bool Knobs

```cpp
Result<bool, KnobError> eval(name, hash_val = nullopt, switch_val = nullopt);
```

```d2 alt="Bool evaluation precedence: a CLI override wins; otherwise look the knob up, then an exact group match, then the knob-wide rollout if a hash is present, then the default"
direction: down
cli: "CLI override\nfor name?" {shape: diamond}
lookup: "Knob in snapshot?" {shape: diamond}
group: "switch_val exactly\nmatches an override?" {shape: diamond}
grouptype: "Forced bool or\ngroup rollout?" {shape: diamond}
rollout: "rollout_pct set and\nhash_val present?" {shape: diamond}
default: "return default"
cli -> lookup: no
cli -> "return override": yes
lookup -> "MissingKnob": no
lookup -> group: yes
group -> grouptype: yes
group -> rollout: no
grouptype -> "return forced value": forced
grouptype -> "bucket < group pct\n(default if no hash)": rollout
rollout -> "bucket < rollout_pct": yes
rollout -> default: no
```

1. **CLI override.** If the process has a CLI override for `name`, return it.
   An int override read with `eval` is `WrongType`.
2. **Lookup.** Find `name` in the current snapshot. Absent is `MissingKnob`; an
   int knob is `WrongType`.
3. **Group override.** If `switch_val` is present and exactly matches a key in
   `overrides`:
   - A forced `true`/`false` returns that value.
   - A `{"rollout_pct": N}` returns `bucket < N` when `hash_val` is present,
     and `default` when it isn't.
4. **Knob-wide rollout.** If `rollout_pct` is set and `hash_val` is present,
   return `bucket < rollout_pct`.
5. **Default.** Return `default`.

An unmatched `switch_val` simply falls through to step 4.

---

## Int Knobs

```cpp
Result<std::int64_t, KnobError> get(name, switch_val = nullopt);
```

1. **CLI override.** If present, return it; a bool override is `WrongType`.
2. **Lookup.** Absent is `MissingKnob`; a bool knob is `WrongType`.
3. Return `default`.

`switch_val` is accepted for symmetry with `eval` and ignored. `get_as<T>`
calls `get`, then converts with a range check: a value that doesn't fit `T` is
`ConversionFailed`, never truncated or wrapped.

---

## The Stable Bucket

```text
input  = UTF8(knob_name) || 0x00 || UTF8(hash_val)
digest = SHA256(input)
u64    = big_endian_uint64(digest[0..8])
bucket = u64 % 100
```

1. **Domain separation.** The zero byte keeps `("ab", "c")` and `("a", "bc")`
   from colliding.
2. **Stickiness.** The same `(knob_name, hash_val)` gives the same bucket on
   every host, in every process and language, across restarts and versions.
3. **Independence.** Including the knob name gives the same entity unrelated
   buckets for different knobs.
4. **Monotonic ramps.** `bucket < pct` means raising `pct` only adds buckets.
5. **Boundaries.** `0` enables nobody; `100` enables every caller that supplies
   a hash value.
6. **No implicit hash.** Without `hash_val`, the rollout is skipped. An empty
   string is a hash value like any other only if you pass one explicitly.

The bucket depends only on the knob name and hash value, never on the group, so
a group rollout and the knob-wide rollout compare the same bucket against
different thresholds.

### Golden Vectors

The test suite pins the hash with golden vectors in
`tests/detent/test_hash_golden.cc`; any reimplementation in another language
should reproduce them. A quick cross-check in Python:

```python
import hashlib

def bucket(name: str, hash_val: str) -> int:
    digest = hashlib.sha256(name.encode() + b"\0" + hash_val.encode()).digest()
    return int.from_bytes(digest[:8], "big") % 100

bucket("payments:new_processor", "user-1")   # 10: off at 10%, on at 50%
bucket("payments:new_processor", "user-42")  # 2:  on at 10%
```

---

## Snapshot Consistency

1. A single read observes exactly one snapshot.
2. Two consecutive reads may observe two different snapshots if an update lands
   between them. Use `read_view()` to pin one snapshot for several reads.
3. Readers never observe a partially applied update.
4. A failed lookup is never cached: a knob that's missing now is found as soon
   as a snapshot that contains it is published.
