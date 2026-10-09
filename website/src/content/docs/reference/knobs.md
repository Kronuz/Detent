---
title: "Knobs and Names"
---

The knob model is deliberately small: two types, one naming rule, and
definitions that are always replaced whole.

---

## Knob Types

| Type | Value | Refinements | Read with |
| --- | --- | --- | --- |
| `bool` | `true` or `false` | `rollout_pct`, `overrides` | `eval` |
| `int` | Signed 64-bit integer | none | `get`, `get_as<T>` |

There are no string, float, list or JSON knobs. A knob's type is fixed by its
definition; reading a bool with `get` or an int with `eval` is a `WrongType`
error.

:::hint[String and structured knobs]{.unimplemented}
Many configuration systems also offer string, number and JSON values, and
per-group values for non-boolean knobs. Detent currently supports only bool and
int knobs, and only bool knobs have group overrides. See the
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

A **bool** definition:

| Field | Required | Meaning |
| --- | --- | --- |
| `type` | yes | `"bool"` |
| `default` | yes | The value when nothing else applies |
| `rollout_pct` | no | Integer `0`–`100`, or `null` for none. Enables that percentage of hash buckets |
| `overrides` | no | Object mapping group names to `true`, `false`, or `{"rollout_pct": N}` |

An **int** definition:

| Field | Required | Meaning |
| --- | --- | --- |
| `type` | yes | `"int"` |
| `default` | yes | The value, a signed 64-bit integer |

Unknown fields are rejected, so a typo like `"rollout_percent"` fails loudly
instead of being ignored.

### Group Names

Group names in `overrides` are opaque, case-sensitive UTF-8 strings of 1 to 128
bytes. Detent doesn't interpret them: `"employees"`, `"region/us-east-1"` and
`"tenant:42"` are all just keys that the caller's switch value must match
exactly.

---

## Whole-Definition Replacement

When a live snapshot or a `PATCH` contains a knob, its definition replaces the
previous one **entirely**. There is no field-level merge:

```json
// Current
{ "type": "bool", "default": false, "rollout_pct": 10,
  "overrides": { "employees": true } }

// PATCH body for this knob
{ "type": "bool", "default": false, "rollout_pct": 20 }

// Result: the employees override is gone
{ "type": "bool", "default": false, "rollout_pct": 20 }
```

The same rule applies when a live snapshot is merged over baked defaults: a
knob present in the live snapshot replaces its baked definition as a whole.
