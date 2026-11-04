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

In C++, read int knobs with `get`, passing the value to use when the knob
can't be read; `lookup` also says why:

```cpp
std::int64_t max_results = reader.get("search:max_results", 25);

auto timeout = reader.lookup("search:timeout_ms");
if (!timeout) log(detent_status_name(timeout.status));
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
if (reader.eval("checkout:new_path", false)) {
    new_checkout();
} else {
    old_checkout();
}
```

:::hint[Name kill switches for what they protect]{.tip}
A common convention is `killswitch_*` for a bool that guards a risky path,
with a safe fallback of `false` at every read. When something breaks,
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

A reader returns each call's fallback for it, with the status `MISSING`, so
remove knob reads from code before removing their definitions.

---

## Fall Back for a Single Read

From the shell, `--default` supplies a value for one read when the knob can't
be read at all:

```sh
detent get checkout:unpublished_feature --default false
# false
```

This doesn't publish or create anything. It's the shell's form of the fallback
every library read takes; see [Errors and Failures](/Detent/reference/errors/).
