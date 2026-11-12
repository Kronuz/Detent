# one-knob-type: One knob type: targeted integers

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status one-knob-type`.

## Outcome

A knob is one thing everywhere: an `int64` definition with a default, per-group overrides (a forced
value, or a percentage that serves the rollout value), and an optional rollout that serves a
rollout value to a percentage of hashes. `BoolDef`, `IntDef`, the type tag and every bool/int branch
are gone from the model, JSON, wire, pages, authority state, CLI and bindings. A bool is default 0
with rollout value 1. The read API is `get(name, hash, group) -> int64` plus `get_as<T>` (`bool`
included); `eval` is removed. Ramping is unchanged: the bucket is still
`stable_bucket(name, hash) < pct` (`src/detent/hashing.cc`), so every caller that is on today stays
on at the same percentage. Integers gain group targeting and gradual rollout.

## Acceptance criteria

- A1 Behavior is preserved for every former bool: for each scenario the existing evaluation tests
  cover (default, rollout at 0/1/50/99/100, group forced, group percentage, missing hash, missing
  group), `get` returns 0/1 exactly where `eval` returned false/true at main, over the same hashes:
  a differential test against values recorded at main, in the suite. The one exception (D68): a
  default-true knob with a percentage, where a hashed caller outside the bucket now gets the
  default (1) instead of false; the test enumerates those cells and asserts exactly that.
- A2 Integers ramp: an int knob with a rollout and group overrides serves its rollout value to the
  expected hashes and groups; invalid shapes (rollout value without a rollout, percentage over 100)
  are refused at publish: tests in the suite.
- A3 Size and speed, measured before and after on the 100K and 5M datasets at named commits:
  page bytes per knob, and the reader's read bench and first read as `exclusive` jobs (D51's
  interleaved-pairs rule: no worse). The design note picks fixed-width or varint fields by these
  numbers and records both.
- A4 No trace of two types: a grep job shows no `BoolDef`, `IntDef`, `is_bool`, `is_int`,
  `bool_count`, `int_count` or a type byte in the page format, and the Python and Rust bindings
  build and pass their suites on the new API.
- A5 [artifact] `docs/design/one-knob-type.md`: the definition, its encoding (with A3's numbers),
  evaluation, JSON shape, and why strings stay out (D65); `docs/design/knob-model.md` (from drop-justknobs) is rewritten for the single type.

## Contracts

Changes the definition's JSON shape, wire encoding, page format and the public read API, with no
compatibility (D43). Every consumer in this repository moves in this quest.

## Decisions

- D65 (owner, 2026-11-11): unify to one targeted integer type; ramping semantics unchanged; no
  string values for now.
- The page format, delivery and readers are also being changed by the namespace quests; this quest
  lands after namespace-s4, and the lead orders it against later namespace steps that touch the
  same files.

## May land early

None.
