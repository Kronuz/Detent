---
title: "Group Overrides"
---

A rollout percentage treats everyone alike. Group overrides let you treat named
groups differently: force a feature on for employees, keep it off for a
sensitive customer segment, or ramp beta users faster than production users.

---

## Force and Ramp by Group

Add an `overrides` object to a bool definition. Each entry maps a group name to
either a forced boolean or its own `rollout_pct`:

```rest
PATCH /v1/snapshot

{
  "schema_version": 1,
  "version": 11,
  "knobs": {
    "payments:new_processor": {
      "type": "bool",
      "default": false,
      "rollout_pct": 10,
      "overrides": {
        "employees": true,
        "excluded": false,
        "beta": { "rollout_pct": 50 },
        "production": { "rollout_pct": 5 }
      }
    }
  }
}
```

Your application supplies the caller's group as the *switch value*:

```sh
sleep 1
detent get payments:new_processor --hash user-1 --switch beta
# true
detent get payments:new_processor --hash user-1 --switch production
# false
detent get payments:new_processor --hash user-1 --switch employees
# true
detent get payments:new_processor --hash user-1 --switch excluded
# false
```

```cpp
bool enabled = client.eval_or_panic("payments:new_processor", user_id, user_group);
```

---

## How a Group Is Evaluated

For a bool knob, Detent checks, in order:

1. A CLI override for this knob in this process, if any.
2. An override whose group name **exactly** matches the switch value:
   - A boolean override returns that value.
   - A rollout override compares the caller's bucket against the group's
     percentage. Without a hash value, it returns the knob's `default`.
3. The knob-wide `rollout_pct`, when a hash value is present.
4. The knob's `default`.

A group that isn't listed falls through to the knob-wide rollout:

```sh
detent get payments:new_processor --hash user-1 --switch unknown
# false   (user-1 is outside the knob-wide 10%)
```

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

```rest
PATCH /v1/snapshot

{
  "schema_version": 1,
  "version": 12,
  "knobs": {
    "payments:new_processor": {
      "type": "bool",
      "default": false,
      "rollout_pct": 10,
      "overrides": {
        "employees": true,
        "excluded": false,
        "beta": { "rollout_pct": 100 },
        "production": { "rollout_pct": 5 }
      }
    }
  }
}
```

Leaving `production` out of that body would delete its override.

:::hint[Int knobs have no group overrides]{.info}
Group overrides and rollouts are bool-only. The int read API accepts a switch
value for symmetry with bool reads, but ignores it.
:::
