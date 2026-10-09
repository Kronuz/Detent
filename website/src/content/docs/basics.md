---
title: "Basic Concepts"
---

There are a few concepts that are core to Detent. Understanding them from the
outset will make everything else much easier to follow.

---

## Knob

A knob is a single named configuration value that a running process reads. It
is either a **bool** knob (a feature flag or kill switch) or an **int** knob (a
limit, a size, a timeout). Knobs never hold strings, floats or structured data;
that restriction keeps evaluation trivial and the read path allocation-free.

Every knob name has exactly two parts separated by one colon:

```text
checkout:killswitch_new_path
└──┬───┘ └────────┬────────┘
knobset     local name
```

Both parts use letters, digits, `.`, `_`, `/` and `-`, and the whole name is
at most 200 bytes. The left part groups related knobs, usually by service or
team. See [Knobs and Names](/Detent/reference/knobs/) for the exact rules.

## Knob Definition

A definition describes a knob's type and how to evaluate it. An int definition
is just a default value. A bool definition is a default plus two optional
refinements: a rollout percentage and per-group overrides.

```json
{ "type": "int", "default": 25 }
{ "type": "bool", "default": false, "rollout_pct": 10,
  "overrides": { "employees": true, "beta": { "rollout_pct": 50 } } }
```

A definition is atomic. Publishing a knob always replaces its **whole**
definition; there is no field-by-field merge.

## Snapshot

A snapshot is a complete, immutable, validated map from knob names to
definitions, stamped with a version number. Readers always see exactly one
snapshot at a time. The background updater builds a brand-new snapshot for
every accepted update and publishes it with a single atomic pointer swap, so
no reader ever observes a half-applied change.

## Version

Every published snapshot carries a positive integer `version`. A client or
distributor only accepts a version greater than the one it already has, so
reordered, repeated or replayed updates are harmless: they are counted as stale
and ignored.

## Baked Defaults

Baked defaults are a snapshot file shipped *with* your binary, conventionally
named `just_knobs.json` or `defaults.json`. They are loaded at startup as
version `0`, before any live data arrives, and they remain the fallback for any
knob that the live snapshot doesn't mention. They are also where new knob names
are introduced.

:::hint[Missing knobs are errors]{.caution}
Detent never invents a value for a knob it doesn't know. Reading a name that is
in neither the live snapshot nor the baked defaults returns a `MissingKnob`
error (or aborts, with the `_or_panic` helpers). Put a safe default in your
baked defaults for every knob your code reads.
:::

## Source

A source is where live snapshots come from. A client consumes exactly one
source: nothing (baked defaults only), a local file that it polls, or an HTTP
distributor. The managed agent adds a fourth, durable immutable generations
delivered from an authenticated authority.

## Distributor

The distributor, `detentd`, is the server that publishers write to and clients
poll. It validates each publish, persists it, keeps a short history of diffs
so clients can download just what changed, and optionally replicates itself
across nodes with Raft.

## Agent

The agent, `detent-agent`, is a host-local daemon that runs one warm client and
republishes every accepted snapshot into a shared-memory file. Shell scripts and
programs in any language read knobs from that file with `detent get` without
talking to the network or to the daemon.

## Override

There are two kinds of override, and they are easy to confuse:

1. **Group overrides** live in a bool knob's definition and apply when the
   caller passes a matching group, the `switch_val`. They are published like
   any other part of a definition.
2. **CLI overrides** are `name=value` pairs given to one process at startup.
   They shadow both the baked defaults and every live snapshot for that process
   only, which makes them ideal for local debugging and tests.
