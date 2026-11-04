---
title: "Kill Switches"
---

A kill switch is a bool knob that guards a risky code path so you can turn it
off without a deploy. This guide walks a change through its whole life: introduce,
roll out, revert if needed, stabilize, and remove.

---

## 1. Introduce

Add the knob read around the new path, with the safe value as its fallback:

```cpp
if (reader.eval("checkout:killswitch_new_path", false)) {
    return new_checkout(cart);
}
return old_checkout(cart);
```

The fallback means a host that has no generation yet, or a name that isn't
published, takes the old path. `reader.lookup("checkout:killswitch_new_path")`
reports why, as a status such as `MISSING` or `UNAVAILABLE`.

---

## 2. Roll Out

Turn it on for employees first, then a small slice of users. Publish this
definition for `checkout:killswitch_new_path`, as an operation in a
[durable authority](/Detent/guides/durable-authority/) transaction:

```json
{ "type": "bool", "default": false, "rollout_pct": 1,
  "overrides": { "employees": true } }
```

```cpp
bool on = reader.eval("checkout:killswitch_new_path", false, account_id, account_group);
```

Ramp the percentage in steps while you watch error rates and latency. See
[Gradual Rollouts](/Detent/rollouts/) and [Group Overrides](/Detent/group-overrides/).

---

## 3. Revert

If anything looks wrong, publish the safe state. Leaving out `rollout_pct` and
`overrides` removes them:

```json
{ "type": "bool", "default": false }
```

Every process takes the old path once its agent publishes the new generation,
with no restart.
Distribution is asynchronous, so plan for a short window where some processes
have flipped and some haven't.

:::hint[Rehearse the revert]{.tip}
Flip the switch off once in staging before you need to in production. It
checks that the knob name in code matches the one you publish, and that the
old path still works.
:::

---

## 4. Stabilize

Once the new path is at 100% and has been healthy long enough, publish it as
the default so hash-less callers get it too:

```json
{ "type": "bool", "default": true }
```

---

## 5. Remove

Knobs are debt. When the old path is no longer needed:

1. Make the new path unconditional in code and delete the knob read.
2. Deploy that everywhere.
3. Delete the live definition: a `null` value in an authority transaction.

Doing it in that order means no running binary reads the knob after it's gone
and falls back to the old path.

:::hint[Stale-flag detection]{.unimplemented}
Detent doesn't track knob ownership, creation dates or expiry, and has no
report of knobs that haven't changed or been read in a long time. Until it
does, keep an owner and a removal date next to each knob in your own records.
:::
