# one-knob-type: journal

State: waiting on jobs 261111-144355-7093c6 (suite at varint commit), A3 261111-144411-793fb6
(corpora), 261111-144413-8bee2e / 261111-144415-4590eb (page bytes 100k/5m),
261111-144417-f31bc6 / 261111-144420-40ce8a (prepare varint / fixed stores)
Quest: .campaign/detent/quests/one-knob-type.md (the lead's; never edit it)

## Doing

Whole tree converted (model, client, JSON, wire, authority, pages, flat image, local, agent,
CLI, bindings, tests, benchmarks, tools, examples, docs). Paged body is varint since
Detent:46e232b7 (fixed width at Detent:7d2c82fc, kept as the A3 comparison arm). First suite
build not yet run (machine queue); compile fixes follow it.

## Next

1. Suite result: fix compile/test failures; then A1/A2 tagged runs (`campaign verify --for A1,A2`).
2. A3: after prepare, `one-knob-type.sh measure PREPARED OUT SIZE` as exclusive jobs, 100k and 5m,
   for prepared-varint (this head) and prepared-fixed (`--commit 7d2c82fc`), CORPORA=
   /Users/kronuz/code/Kronuz/Detent/.scratch/okt/corpora. Fill docs/perf/one-knob-type.md and
   the design note's tables.
3. A4: grep job (BoolDef|IntDef|is_bool|is_int|bool_count|int_count, type byte) and binding suites.
4. A2 mutant: `--fails-without` on `definition_error` (drop the value/percentage pairing check).

## Blocked on

- Resolved: B3 (test_daemon.sh, tests/detent_system oracles/probe/reader_benchmark.hh, website
  reference and architecture pages, diagrams/04_read_path.* added by the lead at Detent:8b17a952).
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
- R6 Paged definition body is varint (docs/design/one-knob-type.md): revision u64, pct, flags,
  then canonical zigzag LEB128 default, count, rollout value; rows len, kind, value (varint or
  pct byte), key. Picked by A3's page bytes: 100K model 49.4 B/record against main 62.1 and
  fixed width 73.2. Strings accounting uses validated key bytes; the warm validator validates
  removed bodies fully instead of decoding headers only.
- R7 Flat image record is 56 bytes (default, override start/count, flags, pct, rollout value);
  override rows 24 bytes (kind, i64 value). Reader metadata DTREAD01 keeps one records count at
  80; 84 is reserved and must be zero.
- R8 `detent get` prints the integer; a former bool prints 0 or 1.
- R9 `global::get_as` delegates to the active client's `get_as`, so a failed conversion through
  the global adapter counts `reads_conversion_failed` (test in tests/detent/test_testing_guard.cc).
- R10 `read_override` checks bounds, kind and percentage only; key UTF-8 and order are checked by
  `validate_definition`, so the read path does not re-check text admission already proved.
