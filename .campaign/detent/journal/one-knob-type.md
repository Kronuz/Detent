# one-knob-type: journal

State: implementing (option 1, D68); model, client, wire committed; local, authority, agent moved
Quest: .campaign/detent/quests/one-knob-type.md (the lead's; never edit it)

## Doing

Converting the tree to one int64 knob. Committed: model, client, JSON, wire. In the tree, to
commit next: pages, flat image, local view/sources, authority state, agent `detent get`, local,
authority, distribution, testing, delivery tests. Second worker on tests/detent, bindings, CLI,
benchmarks, tools/w5, examples, user docs.

## Next

1. Merge the second worker's files; leftover grep; commit; `campaign verify` until it builds green.
2. A1/A2 jobs (A1 test enumerates the D68 cells and asserts 1); A4 grep job and binding suites.
3. A3: docs/perf/one-knob-type script (corpora converted to the new body for the candidate arm),
   page bytes/knob at 100K and 5M, read bench, first read as exclusive interleaved pairs; varint
   page bytes computed from the same corpora.
4. A5: docs/design/one-knob-type.md, knob-model.md rewrite, docs/mechanisms/evaluation.md.

## Blocked on

- B3 (lead): two more files to add to the set: `tests/detent_authority/test_daemon.sh` (sends
  `"type":"int"`) and `tests/detent_system/mixed_oracle.cc` (eval, WrongType, bool_knobs; under
  D68 expects `bucket < pct ? 1 : default`). Asked by message.
- Resolved: B1 (set widened by the lead; src/detent_agent/main.cc, test_agent_cli.cmake,
  test_coordinator.cc added later). B2 ruled option 1 by the owner (D68); the out-of-bucket choice
  lives only in `detent::detail::ramp()` (include/detent/detail/evaluation.hh), which both
  evaluators call.
- Follow-up, not mine: website pages group-overrides.md, quickstart.md, rollouts.md,
  settings-and-flags.md belong to docs-e2e and move to the one-knob model after it lands.
- Left alone as history: docs/reviews, docs/status, docs/handoffs, AUDIT.md, benchmarks/*.json,
  mutant *.patch files of landed quests (tests/detent_delivery/mutants/s3b-k2-*.patch names
  `try_eval` in context only and is not wired into a test).

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | <no evidence yet> |
| A2 | <no evidence yet> |
| A3 | <no evidence yet> |
| A4 | <no evidence yet> |
| A5 | <no evidence yet> |

## Result

Not yet.

## Rulings I made

- R1 JSON: no `type`; `rollout_value` is required when the definition has any percentage
  (knob-wide or group) and refused otherwise (explicit; A2's "rollout value without a rollout").
- R2 `get_as<bool>`: 0 is false, 1 is true, anything else ConversionFailed (never truncated).
- R3 CLI overrides: one `int64`; `true`/`false` stay accepted as 1/0.
- R4 JSON refuses boolean literals and `type`; `schema_version` stays 1 (greenfield, D43).
- R5 Bindings: Python `get`/`get_bool`, Rust `get`/`get_bool` mirror `get`/`get_as<bool>`.
- R6 Paged definition body is fixed width: 20-byte header (revision, default, pct/255, flags,
  count u16) plus an 8-byte rollout value when present; override rows are a 10-byte header
  (len, kind, i64 value) plus key. Fixed width keeps strings accounting a function of sizes;
  A3 measures it against varint.
- R7 Flat image record is 56 bytes (default, override start/count, flags, pct, rollout value);
  override rows 24 bytes (kind, i64 value). Reader metadata DTREAD01 keeps one records count at
  80; 84 is reserved and must be zero.
- R8 `detent get` prints the integer; a former bool prints 0 or 1.
