---
title: "Settings and Flags"
---

The two everyday uses of Detent are changing a number and flipping a switch.
These examples continue from the [Quick-start](/Detent/quickstart/), in the
same directory, with the `publish` helper defined and the snapshot at
version 2.

---

## Change a Numeric Setting

Int knobs hold any signed 64-bit integer. Use them for limits, batch sizes,
timeouts in milliseconds, or anything else you'd otherwise hard-code:

```sh
publish <<'EOF'
{
  "schema_version": 1,
  "version": 3,
  "knobs": {
    "search:max_results": { "type": "int", "default": 500 },
    "search:timeout_ms": { "type": "int", "default": 250 }
  }
}
EOF
```

```sh
sleep 1
detent get search:max_results
# 500
detent get search:timeout_ms
# 250
```

The file is the whole live snapshot. Each publish lists every knob you want
live, so the examples below repeat the knobs from earlier steps.

In C++, read int knobs with `get`, or with `get_as<T>` for a checked
conversion that fails instead of truncating:

```cpp
std::int64_t max_results = client.get_or_panic("search:max_results");

auto timeout = client.get_as<std::uint32_t>("search:timeout_ms");
if (!timeout) log(timeout.error().what());
```

---

## Toggle a Feature

Bool knobs are feature flags and kill switches. Turn a new checkout path on:

```sh
publish <<'EOF'
{
  "schema_version": 1,
  "version": 4,
  "knobs": {
    "search:max_results": { "type": "int", "default": 500 },
    "search:timeout_ms": { "type": "int", "default": 250 },
    "checkout:new_path": { "type": "bool", "default": true }
  }
}
EOF
```

```sh
sleep 1
detent get checkout:new_path
# true
```

And off again, if anything goes wrong:

```sh
publish <<'EOF'
{
  "schema_version": 1,
  "version": 5,
  "knobs": {
    "search:max_results": { "type": "int", "default": 500 },
    "search:timeout_ms": { "type": "int", "default": 250 },
    "checkout:new_path": { "type": "bool", "default": false }
  }
}
EOF
```

```sh
sleep 1
detent get checkout:new_path
# false
```

In C++, the same read is `eval`:

```cpp
if (client.eval_or_panic("checkout:new_path")) {
    new_checkout();
} else {
    old_checkout();
}
```

:::hint[Name kill switches for what they protect]{.tip}
A common convention is `killswitch_*` for a bool that guards a risky path,
with a safe default of `false` in your baked defaults. When something breaks,
flipping it is one publish away, and every process picks it up within one poll
interval. The [Kill Switches](/Detent/guides/kill-switches/) guide covers the
full lifecycle.
:::

---

## Remove a Knob from the Live Snapshot

Leave a knob out of the next snapshot to remove it from the live snapshot:

```sh
publish <<'EOF'
{
  "schema_version": 1,
  "version": 6,
  "knobs": {
    "search:max_results": { "type": "int", "default": 500 },
    "checkout:new_path": { "type": "bool", "default": false }
  }
}
EOF
```

A client whose baked defaults contain `search:timeout_ms` falls back to that
baked value. A client without one now gets a `MissingKnob` error for it, so
remove knob reads from code before removing their definitions.

---

## Fall Back for a Single Read

From the shell, `--default` supplies a value for one read when the knob can't
be read at all:

```sh
detent get checkout:unpublished_feature --default false
# false
```

This doesn't publish or create anything. It's a per-call fallback for scripts
that must keep going. The C++ library deliberately has no equivalent; see
[Errors and Failures](/Detent/reference/errors/) for why.
