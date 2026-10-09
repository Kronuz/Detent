---
title: "Gradual Rollouts"
---

Bool knobs can enable a feature for a percentage of entities, such as users,
accounts or hosts, instead of everyone at once. Ramp up while you watch your
dashboards, and roll back instantly if something looks wrong.

---

## Start Small

Add `rollout_pct` to a bool definition to enable it for that percentage of
hash buckets:

```rest
PATCH /v1/snapshot

{
  "schema_version": 1,
  "version": 7,
  "knobs": {
    "payments:new_processor": {
      "type": "bool", "default": false, "rollout_pct": 10
    }
  }
}
```

Each read passes the entity's stable identifier as the *hash value*:

```sh
sleep 1
detent get payments:new_processor --hash user-1
# false
detent get payments:new_processor --hash user-42
# true
```

```cpp
bool enabled = client.eval_or_panic("payments:new_processor", user_id);
```

The same identifier always lands in the same bucket, across reads, processes,
hosts, languages and restarts. `user-42` stays enabled until you change the
knob.

---

## How Buckets Work

Detent hashes the knob name and the hash value together with SHA-256, takes the
first eight bytes as a big-endian integer, and reduces it modulo 100:

```text
bucket = u64_be(SHA256(knob_name || 0x00 || hash_val)[0..8]) % 100
enabled = bucket < rollout_pct
```

Three properties fall out of that:

1. **Sticky.** A given `(knob, hash value)` pair always maps to the same bucket.
2. **Monotonic.** Raising the percentage only adds buckets, so everyone already
   enabled stays enabled as you ramp up.
3. **Independent.** The knob name is part of the input, so the same user lands
   in unrelated cohorts for different knobs; your 10% beta testers aren't
   always the same 10% of people.

`0` enables nobody, and `100` enables everybody who supplies a hash value.

:::hint[Buckets, not exact counts]{.info}
A percentage describes hash buckets, not a quota. With a small population, a
10% rollout won't split exactly 10/90; with thousands of users it converges.
:::

---

## Ramp Up

Publish higher percentages as you gain confidence:

```rest
PATCH /v1/snapshot

{
  "schema_version": 1,
  "version": 8,
  "knobs": {
    "payments:new_processor": {
      "type": "bool", "default": false, "rollout_pct": 50
    }
  }
}
```

```rest
PATCH /v1/snapshot

{
  "schema_version": 1,
  "version": 9,
  "knobs": {
    "payments:new_processor": {
      "type": "bool", "default": false, "rollout_pct": 100
    }
  }
}
```

```sh
sleep 1
detent get payments:new_processor --hash user-1
# true
```

:::hint[No hash, no rollout]{.caution}
A read *without* a hash value skips the rollout entirely and returns the knob's
`default`. At 100% with `"default": false`, a hash-less read still returns
`false`. Detent never hashes an empty string on your behalf.
:::

---

## Finish or Roll Back

To finish the rollout for every caller, hashed or not, publish the knob with
the new default and no rollout:

```rest
PATCH /v1/snapshot

{
  "schema_version": 1,
  "version": 10,
  "knobs": {
    "payments:new_processor": { "type": "bool", "default": true }
  }
}
```

To roll back everyone, publish `"default": false` without `rollout_pct`
instead. Because a publish replaces the whole definition, leaving out
`rollout_pct` removes the gate.

When the feature is fully launched and stable, make the new path unconditional
in code, then remove the knob read, its live definition and its baked default.
