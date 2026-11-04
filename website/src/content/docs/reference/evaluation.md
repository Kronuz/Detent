---
title: "Evaluation Semantics"
---

These are the exact rules every Detent reader follows: `detent::Reader` in
C++, the Python and Rust bindings over the same C ABI, and `detent get`.

---

## Bool Knobs

```cpp
bool eval(name, fallback, hash = nullopt, switch_value = nullopt);
```

```d2 alt="Bool evaluation precedence: look the knob up, then an exact group match, then the knob-wide rollout if a hash is present, then the default; any error returns the caller's fallback"
direction: down
lookup: "Knob in generation\nand a bool?" {shape: diamond}
group: "switch_value exactly\nmatches an override?" {shape: diamond}
grouptype: "Forced bool or\ngroup rollout?" {shape: diamond}
rollout: "rollout_pct set and\nhash present?" {shape: diamond}
default: "return default"
lookup -> "return fallback\n(status says why)": no
lookup -> group: yes
group -> grouptype: yes
group -> rollout: no
grouptype -> "return forced value": forced
grouptype -> "bucket < group pct\n(default if no hash)": rollout
rollout -> "bucket < rollout_pct": yes
rollout -> default: no
```

1. **Lookup.** Find `name` in the attached generation. Absent is `MISSING`; an
   int knob is `WRONG_TYPE`. Either returns `fallback`.
2. **Group override.** If `switch_value` is present and exactly matches a key
   in `overrides`:
   - A forced `true`/`false` returns that value.
   - A `{"rollout_pct": N}` returns `bucket < N` when `hash` is present, and
     `default` when it isn't.
3. **Knob-wide rollout.** If `rollout_pct` is set and `hash` is present,
   return `bucket < rollout_pct`.
4. **Default.** Return the knob's `default`.

An unmatched `switch_value` simply falls through to step 3. The knob's
`default` is the published one; the caller's `fallback` answers only when the
knob can't be read.

---

## Int Knobs

```cpp
std::int64_t get(name, fallback);
```

1. **Lookup.** Absent is `MISSING`; a bool knob is `WRONG_TYPE`. Either
   returns `fallback`.
2. Return the knob's `default`.

`lookup(name, hash, switch_value)` runs the same rules for either type and
returns the status with the value, which carries its kind: bool or int.

---

## The Stable Bucket

```text
input  = UTF8(knob_name) || 0x00 || UTF8(hash)
digest = SHA256(input)
u64    = big_endian_uint64(digest[0..8])
bucket = u64 % 100
```

1. **Domain separation.** The zero byte keeps `("ab", "c")` and `("a", "bc")`
   from colliding.
2. **Stickiness.** The same `(knob_name, hash)` gives the same bucket on
   every host, in every process and language, across restarts and versions.
3. **Independence.** Including the knob name gives the same entity unrelated
   buckets for different knobs.
4. **Monotonic ramps.** `bucket < pct` means raising `pct` only adds buckets.
5. **Boundaries.** `0` enables nobody; `100` enables every caller that supplies
   a hash value.
6. **No implicit hash.** Without `hash`, the rollout is skipped. An empty
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

## Generation Consistency

1. A single read observes exactly one generation.
2. Two consecutive reads may observe two different generations if a publish
   lands between them. `revision(namespace)` reports the revision attached.
3. Readers never observe a partially applied update.
4. A failed lookup is never cached: a knob that's missing now is found as soon
   as a generation that contains it is published.
