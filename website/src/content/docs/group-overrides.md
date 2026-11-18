---
title: "Group Overrides"
---

A rollout percentage treats everyone alike. Group overrides let you treat named
groups differently: force a feature on for employees, keep it off for a
sensitive customer segment, or ramp beta users faster than production users.

---

## Force and Ramp by Group

Add an `overrides` object to a definition. Each entry maps a group name to
either a forced value or its own `rollout_pct`, which serves the knob's
`rollout_value` to that percentage of the group:

```sh
publish payments:new_processor '{
  "default": 0, "rollout_pct": 10, "rollout_value": 1,
  "overrides": {
    "employees": 1, "excluded": 0,
    "beta": {"rollout_pct": 50}, "production": {"rollout_pct": 5}
  }
}'
```

<!-- e2e:begin
stdout-matches: "status":"applied"
e2e:end -->

Your application supplies the caller's group as the *switch value*:

```sh
sleep 1
detent get payments:new_processor --hash user-1 --switch beta
# 1
detent get payments:new_processor --hash user-1 --switch production
# 0
detent get payments:new_processor --hash user-1 --switch employees
# 1
detent get payments:new_processor --hash user-1 --switch excluded
# 0
```

<!-- e2e:begin
stdout:
  1
  0
  1
  0
e2e:end -->

```cpp
bool enabled = client.get_or_panic("payments:new_processor", user_id, user_group) == 1;
```

---

## How a Group Is Evaluated

Detent checks, in order:

1. A CLI override for this knob in this process, if any.
2. An override whose group name **exactly** matches the switch value:
   - A forced override returns its value.
   - A rollout override returns `rollout_value` when the caller's bucket is
     below the group's percentage, and `default` otherwise or without a hash
     value.
3. The knob-wide `rollout_pct`, when a hash value is present: `rollout_value`
   inside the bucket, `default` outside it.
4. The knob's `default`.

A group that isn't listed falls through to the knob-wide rollout:

```sh
detent get payments:new_processor --hash user-1 --switch unknown
# 0   (user-1 is outside the knob-wide 10%)
```

<!-- e2e:begin
stdout: 0
e2e:end -->

A group rollout uses the **same bucket** as the knob-wide rollout. `user-1`'s
bucket for `payments:new_processor` is the same number whatever group it's
evaluated in; only the threshold changes. That makes the result predictable:
anyone enabled at 5% in `production` is also enabled at 10% knob-wide and at
50% in `beta`.

:::hint[Group names are exact and case-sensitive]{.caution}
`Beta` and `beta` are different groups. Group names are opaque UTF-8 strings
of 1 to 128 bytes; Detent doesn't normalize or interpret them.
:::

---

## Changing One Group

A publish replaces a knob's **whole** definition, overrides included. To change
the `beta` percentage, send the complete definition with every group you want to
keep:

```sh
detent get payments:new_processor --hash user-2 --switch beta
# 0
publish payments:new_processor '{
  "default": 0, "rollout_pct": 10, "rollout_value": 1,
  "overrides": {
    "employees": 1, "excluded": 0,
    "beta": {"rollout_pct": 100}, "production": {"rollout_pct": 5}
  }
}'
sleep 1
detent get payments:new_processor --hash user-2 --switch beta
# 1
```

<!-- e2e:begin
stdout-matches: ^0\n
stdout-matches: ^1$
e2e:end -->

`user-2` falls outside the old 50% for this knob, so it reads 0 in `beta`
before the publish and 1 after. Leaving `production` out of that
definition would delete its override.

:::hint[Any value, not only flags]{.info}
Overrides and rollouts work the same for every knob. A batch size of 25 that
the `beta` group sees as 7 and 10% of callers see as 50 is
`{"default": 25, "rollout_pct": 10, "rollout_value": 50, "overrides": {"beta": 7}}`.
:::
