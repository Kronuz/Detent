# one-knob-type: journal

State: blocked on B4 (fixtures outside the set) and waiting on jobs at Detent:b9bf610c: suite
261111-162754-7422a7, docs e2e 261111-162755-40ef2b, A4 261111-162758-63e456, A2 mutants
261111-162800-2ee71e / 261111-162803-4b354d, A3 prepare 261111-162827-7366e8, measure pairs 100k
261111-162829-50dcc0 .. 261111-162838-020154, 5m 261111-162839-4759eb .. 261111-162848-77259b,
pages 261111-162851-fc6aaf (100k) / 261111-162856-b47db3 (5m)
Quest: .campaign/detent/quests/one-knob-type.md (the lead's; never edit it)

## Doing

Session 2. The tree builds (keep-going build 261111-153615-1a7748). Suite 261111-155807-cb02cd
found: the overlay admitted definition bodies from the old 24-byte header only ("overlay leaf
framing": every delta, managed agent, relay and docs e2e failure), old wire tuples in fixtures,
sed damage in test_model_and_schema.cc, fixture sizes tied to the fixed width (separate-segment
reader fixture, trusted-restart stale rows), and integer expectations; all in-set ones fixed at
Detent:9db7c5fd and Detent:b9bf610c. Out-of-set fixtures: B4.

## Next

1. Lead rules B4; then convert those four files, merge main (CHANGELOG moved), and rerun every
   evidence job at the final head (`campaign job rerun` the A3 ones, or resubmit).
2. Read the suite at b9bf610c for anything left (test_local_dense_reads now names its line).
3. A3: after the 12 pair jobs, `docs/perf/one-knob-type.sh report OUT SIZE` (level none) per size;
   fill the design note's speed table and docs/perf/one-knob-type.md.
4. Result and the Evidence table from `campaign brief`.

## CHANGELOG entry (for the lead)

- One knob type: every knob is a signed 64-bit integer with a default, group overrides (a forced
  value or a percentage) and an optional rollout serving `rollout_value`; a flag is default 0,
  rollout value 1. `BoolDef`, `IntDef`, the JSON `type` field, `eval` and `eval_or_panic` are
  gone; reads are `get`, `get_as<T>` (`get_as<bool>` takes 0 and 1) and `get_or_panic`. A hashed
  caller outside a percentage gets `default` (D68). JSON, wire, paged body (varint, 20% fewer page
  bytes than the two-type body), flat image and reader metadata change with no compatibility.

## Blocked on

- B4 (file set): five test fixtures outside the set hand-encode main's integer definition
  `[1, value]` on the wire and fail with "distribution tuple arity mismatch" (suite
  261111-155807-cb02cd): tests/detent_delivery/scripted_upstream.hh:137 and :202 (test_repair,
  test_hash_once, relay_delta_*), tests/detent_delivery/probe_fixture.hh:71 (test_probe_client,
  test_probe_response), tests/detent_delivery/test_response.cc:72,
  tests/detent_agent/test_agent_catch_up.cc:32 (`put_definition`). The fix is mechanical: write
  `[value, nil, nil, []]` (`detail::wire::definition(out, WireDefinition{value, {}, {}, {}})`).
  Options: 1. add these four files to the set (recommended: the quest's contract says every
  consumer moves here); 2. a follow-up quest, landing this one with those tests red (not
  acceptable for A1). Waiting for the lead's ruling; every other suite failure is in the set.
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

- R11 The fixed-width arm is measured by the dense model only: the model equals main's C++ full
  build exactly (3,245 pages, 6,209,298 B on 100K), and the pages jobs check the varint C++ build
  against the model too. Speed is measured for the chosen body (varint) against main; the fixed
  arm loses by 18-19% page bytes before speed matters.
- R12 A1 records main's answers (one_knob_record.sh at `--ref BASELINE=main`) rather than keeping
  a frozen copy of main's evaluator: the recording is main's own code, the test reads its bits.
- R13 Exclusive timing jobs are capped at 10 minutes, so A3 timing runs one D51 pair per job
  (`measure ... A-B`) and `report` takes the verdicts over all six.

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
