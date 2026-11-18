---
title: "Knobs and Names"
---

The knob model is deliberately small: one value type, one naming rule, and
definitions that are always replaced whole.

---

## Knob Values

Every knob holds one signed 64-bit integer. A feature flag or kill switch is a
knob whose values are 0 and 1; a limit, a size or a timeout uses the whole
range. Every knob supports a rollout percentage and group overrides.

| Read with | Returns |
| --- | --- |
| `get` | The `int64` value |
| `get_as<T>` | The value converted to `T`; a value that does not fit is `ConversionFailed` |
| `get_as<bool>` | `false` for 0, `true` for 1; any other value is `ConversionFailed` |

There are no string, float, list or JSON knobs.

:::hint[String and structured knobs]{.unimplemented}
Many configuration systems also offer string, number and JSON values. Detent
supports only 64-bit integers. See the
[Roadmap](/Detent/roadmap/#knob-model).
:::

---

## Knob Names

A knob name is a **knobset** and a **local name** joined by exactly one colon:

```text
^[A-Za-z0-9._/-]+:[A-Za-z0-9._/-]+$
```

1. Exactly one `:`, with a non-empty part on each side.
2. Each part uses ASCII letters, digits, `.`, `_`, `/` and `-`.
3. At most 200 bytes in total.
4. Names are case-sensitive.

Valid names:

```text
checkout:killswitch_new_path
search:max_results
pytorch/torchrec:killswitch_safe_tolist
ads.ranking:model-v2.enabled
```

Invalid names:

```text
max_results              # no colon
search:max:results       # two colons
search:max results       # space
:max_results             # empty knobset
```

An invalid name is rejected everywhere: in a snapshot, which fails the whole
update, and on a read, which returns `InvalidName`.

The knobset is also the unit of authorization and atomicity in the
[durable authority](/Detent/guides/durable-authority/), where it's called a
*namespace*.

---

## Definitions

A definition:

| Field | Required | Meaning |
| --- | --- | --- |
| `default` | yes | A signed 64-bit integer: the value when nothing else applies |
| `overrides` | no | Object mapping group names to an integer, which forces that value, or `{"rollout_pct": N}` |
| `rollout_pct` | no | Integer `0`–`100`, or `null` for none. Callers in that percentage of hash buckets get `rollout_value` |
| `rollout_value` | with a percentage | A signed 64-bit integer, or `null` for none: the value callers inside a percentage get |

`rollout_value` is present exactly when a definition has a percentage, either
`rollout_pct` or a group's `{"rollout_pct": N}`. A caller outside the
percentage gets `default`. Values are JSON integers; `true`, `false` and
`1.0` are rejected. Unknown fields are rejected, so a typo like
`"rollout_percent"` fails loudly instead of being ignored.

A flag ramping to 10% of users, forced on for employees:

```json
{ "default": 0, "overrides": { "employees": 1 }, "rollout_pct": 10, "rollout_value": 1 }
```

A batch size of 25 that 10% of users see as 50, and the `beta` group as 7:

```json
{ "default": 25, "overrides": { "beta": 7 }, "rollout_pct": 10, "rollout_value": 50 }
```

### Group Names

Group names in `overrides` are opaque, case-sensitive UTF-8 strings of 1 to 128
bytes. Detent doesn't interpret them: `"employees"`, `"region/us-east-1"` and
`"tenant:42"` are all just keys that the caller's switch value must match
exactly.

---

## Whole-Definition Replacement

When a live snapshot, a patch or a transaction contains a knob, its definition replaces the
previous one **entirely**. There is no field-level merge:

```json
// Current
{ "default": 0, "overrides": { "employees": 1 },
  "rollout_pct": 10, "rollout_value": 1 }

// New definition for this knob
{ "default": 0, "rollout_pct": 20, "rollout_value": 1 }

// Result: the employees override is gone
{ "default": 0, "rollout_pct": 20, "rollout_value": 1 }
```

The same rule applies when a live snapshot is merged over baked defaults: a
knob present in the live snapshot replaces its baked definition as a whole.
