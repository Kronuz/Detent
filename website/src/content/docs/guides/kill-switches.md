---
title: "Kill Switches"
---

A kill switch is a knob with values 0 and 1 that guards a risky code path so you can turn it
off without a deploy. This guide walks a change through its whole life: introduce,
roll out, revert if needed, stabilize, and remove.

---

## 1. Introduce

Add the knob read around the new path, and its safe default to your baked
defaults, **in the same change**:

```cpp
if (client.get_or_panic("checkout:killswitch_new_path") == 1) {
    return new_checkout(cart);
}
return old_checkout(cart);
```

```json title="defaults.json"
{
  "schema_version": 1,
  "knobs": {
    "checkout:killswitch_new_path": { "default": 0 }
  }
}
```

The safe default means a host that has never heard from its source, or one
that's partitioned from it, takes the old path. Shipping the read without the
default is what makes a missing knob possible, and Detent treats a missing knob
as an error, not as 0.

---

## 2. Roll Out

Turn it on for employees first, then a small slice of users. Publish this
definition for `checkout:killswitch_new_path`, as an operation in a
[durable authority](/Detent/guides/durable-authority/) transaction or in the
next version of a live snapshot file:

```json
{ "default": 0, "overrides": { "employees": 1 },
  "rollout_pct": 1, "rollout_value": 1 }
```

```cpp
bool on = client.get_or_panic("checkout:killswitch_new_path", account_id, account_group) == 1;
```

Ramp the percentage in steps while you watch error rates and latency. See
[Gradual Rollouts](/Detent/rollouts/) and [Group Overrides](/Detent/group-overrides/).

---

## 3. Revert

If anything looks wrong, publish the safe state. Leaving out `overrides`, `rollout_pct` and
`rollout_value` removes them:

```json
{ "default": 0 }
```

Every process takes the old path within one poll interval, with no restart.
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
{ "default": 1 }
```

---

## 5. Remove

Knobs are debt. When the old path is no longer needed:

1. Make the new path unconditional in code and delete the knob read.
2. Deploy that everywhere.
3. Delete the live definition: a `null` value in an authority transaction, or
   a snapshot file that leaves the knob out.
4. Remove the baked default.

Doing it in that order means no running binary ever reads a knob that no longer
exists.

:::hint[Stale-flag detection]{.unimplemented}
Detent doesn't track knob ownership, creation dates or expiry, and has no
report of knobs that haven't changed or been read in a long time. Until it
does, keep an owner and a removal date next to each knob in your own records.
:::
