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
definitions, stamped with a version number. The agent publishes each accepted
snapshot as a new immutable generation on disk, and a reader switches to it on
its next lookup, so no reader ever observes a half-applied change.

## Version

Every published snapshot carries a positive integer `version`, and each
namespace's generation carries its revision. A reader refuses a revision that
goes backwards, so reordered, repeated or replayed updates are harmless.

## Fallback

Every read passes its own fallback: the value the caller gets when the knob
can't be read. There is no defaults file; the safe value lives next to the code
that reads the knob.

:::hint[Missing knobs return the fallback]{.caution}
Detent never invents a value for a knob it doesn't know. Reading a name that
the generation doesn't hold returns the caller's fallback, and `lookup` reports
the status, `MISSING`. A misspelled name is silent unless you check that
status.
:::

## Source

A source is where live snapshots come from. Readers have one: the managed
agent's store directory, where `detent-agent` publishes durable immutable
generations delivered from an authenticated authority. A reader opens the store
with the uid the agent runs as and maps the generations read-only.

## Authority

The durable authority, `detentd --authority`, is the server that publishers
write to. It authenticates each publisher, applies atomic, per-namespace
transactions, commits them through durable Raft consensus, and delivers the
result to managed agents.

## Agent

The agent, `detent-agent`, is a host-local daemon that receives authenticated
deliveries from the authority and publishes immutable generations on disk. Shell
scripts and programs in any language read knobs from them with `detent get`
without talking to the network or to the daemon.

## Group Override

A group override lives in a bool knob's definition and applies when the caller
passes a matching group, the `switch_value`. It is published like any other
part of a definition.
