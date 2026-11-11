# one-knob-type: journal

State: waiting on jobs 261111-153615-1a7748 (keep-going build: every compile error at once),
261111-154349-807bef (A3 dense-model page bytes, main/fixed/varint, 100k and 5m)
Quest: .campaign/detent/quests/one-knob-type.md (the lead's; never edit it)

## Doing

Session 2. Fixed the one known compile error (protocol.cc DefinitionDecoder member order,
Detent:6eb6e566's parent), merged main (docs-e2e), moved the four docs-e2e pages and
tests/docs/check_docs.py to the single type (check_docs.py's knob-type space removed). Waiting for
the keep-going build's error list.

## Next

1. Fix every error from 261111-153615-1a7748's log; then `campaign verify one-knob-type --for
   A1,A2` (suite) and a docs e2e job (`tests/docs/run_e2e.py`).
2. A3: the fixed arm at Detent:7d2c82fc has the same protocol.cc error, so its C++ numbers need a
   compiling fixed-width tree: a side commit off the green head with the fixed body restored
   (only for page bytes from the C++ full build; the model job covers both bodies). Then pages and
   prepare jobs, then `measure` as exclusive jobs (100k, 5m) for the varint head against main.
   Reconcile the design note's table (lead finding): define the columns (page bytes = 64 B page
   headers + node bytes (leaf 4 + key suffix + body; branch) + 68 B per external, pages are
   variable length up to 4 KiB, never padded) and take numbers only from jobs; the old main row
   was the C++ full build and the fixed/varint rows hand model runs.
3. A4: grep job over src include bindings tests tools examples benchmarks (excluding
   benchmarks/container-alternative/*.patch, history prototypes) plus a build with
   DETENT_BUILD_PYTHON/RUST and targets detent_python_test, detent_rust_test.
4. A2 mutant: `--fails-without` on `definition_error`.

## Blocked on

- Resolved: B3 (test_daemon.sh, tests/detent_system oracles/probe/reader_benchmark.hh, website
  reference and architecture pages, diagrams/04_read_path.* added by the lead at Detent:8b17a952).
- Resolved: B1 (set widened by the lead; src/detent_agent/main.cc, test_agent_cli.cmake,
  test_coordinator.cc added later). B2 ruled option 1 by the owner (D68); the out-of-bucket choice
  lives only in `detent::detail::ramp()` (include/detent/detail/evaluation.hh), which both
  evaluators call.
- Done: the docs-e2e pages and tests/docs/** moved to the single type (now in the set).
- For the lead, outside my set: docs/dev/docs-e2e.md:30 and docs/design/observability_bench.cc
  (not built) still publish `"type":"int"`/`"type":"bool"` definitions.
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
