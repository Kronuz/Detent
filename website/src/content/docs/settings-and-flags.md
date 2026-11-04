---
title: "Settings and Flags"
---

The two everyday uses of Detent are changing a number and flipping a switch.
These examples continue from the [Quick-start](/Detent/quickstart/), in the
same shell and directory, with the authority and the agent running and the
`publish` helper defined.

---

## Change a Numeric Setting

Int knobs hold any signed 64-bit integer. Use them for limits, batch sizes,
timeouts in milliseconds, or anything else you'd otherwise hard-code:

```sh
publish search:max_results '{"type":"int","default":500}'
publish search:timeout_ms '{"type":"int","default":250}'
sleep 1
detent get search:max_results
# 500
detent get search:timeout_ms
# 250
```

Each transaction replaces the whole definition of the key it names and leaves
every other knob alone, so a publish only lists the knob it changes.

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
publish checkout:new_path '{"type":"bool","default":true}'
sleep 1
detent get checkout:new_path
# true
```

And off again, if anything goes wrong:

```sh
publish checkout:new_path '{"type":"bool","default":false}'
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
flipping it is one publish away, and every managed agent delivers it to the
readers on its host. The [Kill Switches](/Detent/guides/kill-switches/) guide
covers the full lifecycle.
:::

---

## Remove a Knob

A `null` value deletes the key from its namespace:

```sh
publish search:timeout_ms null
sleep 1
detent get search:timeout_ms
# ERR MissingKnob: search:timeout_ms (scoped lookup failed)
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
