---
title: "Gradual Rollouts"
---

A knob can serve a new value to a percentage of entities, such as users,
accounts or hosts, instead of everyone at once. Ramp up while you watch your
dashboards, and roll back instantly if something looks wrong.

---

## Start Small

Add `rollout_pct` and `rollout_value` to a definition to serve that value to
that percentage of hash buckets; every other caller gets `default`. A flag
ramps from `default` 0 to `rollout_value` 1. These examples continue in the [Quick-start](/Detent/quickstart/)
shell and directory, with the `publish` helper defined:

```sh
publish payments:new_processor '{"default":0,"rollout_pct":10,"rollout_value":1}'
```

<!-- e2e:begin
stdout-matches: "status":"applied"
e2e:end -->

Each read passes the entity's stable identifier as the *hash value*:

```sh
sleep 1
detent get payments:new_processor --hash user-1
# 0
detent get payments:new_processor --hash user-42
# 1
```

<!-- e2e:begin
stdout:
  0
  1
e2e:end -->

```cpp
bool enabled = client.get_or_panic("payments:new_processor", user_id) == 1;
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
value = bucket < rollout_pct ? rollout_value : default
```

Three properties fall out of that:

1. **Sticky.** A given `(knob, hash value)` pair always maps to the same bucket.
2. **Monotonic.** Raising the percentage only adds buckets, so everyone already
   enabled stays enabled as you ramp up.
3. **Independent.** The knob name is part of the input, so the same user lands
   in unrelated cohorts for different knobs; your 10% beta testers aren't
   always the same 10% of people.

`0` serves `rollout_value` to nobody, and `100` to everybody who supplies a
hash value. The same rule ramps any integer: a batch size of 25 that 10% of
callers see as 50 is `{"default":25,"rollout_pct":10,"rollout_value":50}`.

:::hint[Buckets, not exact counts]{.info}
A percentage describes hash buckets, not a quota. With a small population, a
10% rollout won't split exactly 10/90; with thousands of users it converges.
:::

---

## Ramp Up

Publish higher percentages as you gain confidence:

```sh
publish payments:new_processor '{"default":0,"rollout_pct":50,"rollout_value":1}'
# Check your dashboards before advancing.
publish payments:new_processor '{"default":0,"rollout_pct":100,"rollout_value":1}'
sleep 1
detent get payments:new_processor --hash user-1
# 1
detent get payments:new_processor
# 0
```

<!-- e2e:begin
stdout-matches: ^1\n0$
e2e:end -->

:::hint[No hash, no rollout]{.caution}
A read *without* a hash value skips the rollout entirely and returns the knob's
`default`. At 100% with `"default": 0`, a hash-less read still returns
0. Detent never hashes an empty string on your behalf.
:::

---

## Finish or Roll Back

To finish the rollout for every caller, hashed or not, publish the knob with
the new default and no rollout:

```sh
publish payments:new_processor '{"default":1}'
sleep 1
detent get payments:new_processor
# 1
```

<!-- e2e:begin
stdout-matches: ^1$
e2e:end -->

To roll back everyone, publish `"default": 0` without `rollout_pct`
instead. Because a publish replaces the whole definition, leaving out
`rollout_pct` and `rollout_value` removes the gate.

When the feature is fully launched and stable, make the new path unconditional
in code, then remove the knob read, its live definition and its baked default.
