# one-knob-type: journal

State: waiting on jobs SUITE_IDS (suite after the S6 observability deletion and the main merge).
A3 written up at Detent:dad4d5f5 from the jobs at Detent:dab00c10; A1, A2, A4 met.
Quest: .campaign/detent/quests/one-knob-type.md (the lead's; never edit it)

## Doing

Session 5. Merged main (Detent:d734d036; only campaign files and
docs/design/async-drivers-bench, wheel-evidence.md, superseded-libraries.md came in). Deleted the
S6 observability harness and its design note at the lead's instruction (Detent:7aed2593):
docs/design/observability-bench.sh, observability-summary.py, observability-paired.py,
observability.md (observability_bench.cc went at Detent:9f677f2c; `git grep` finds no other user).
Remaining mentions of docs/design/observability.md are in docs/dev/docs-site-audit.md (:192, :416,
:673, :788, :1208, :1263, :1566, :1568), outside the set: a dated audit, left for the lead.

A3 at the head (Detent:dad4d5f5, docs/perf/one-knob-type.md, docs/design/one-knob-type.md):
D51 rule (median paired difference at most the noise floor), all NO WORSE.

| Corpus | Metric | Main | One-knob | Paired median | Noise floor | Faster |
| --- | --- | --- | --- | --- | --- | --- |
| 100K | first read | 645.0 us | 600.5 us | -44.4 us (-6.9%) | 87.0 us | 4/6 |
| 100K | steady read | 1,212 ns | 1,099 ns | -126 ns (-10.4%) | 30 ns | 6/6 |
| 5M | first read | 716.6 us | 575.5 us | -123.5 us (-17.2%) | 132.0 us | 5/6 |
| 5M | steady read | 1,929 ns | 1,769 ns | -179 ns (-9.3%) | 212 ns | 5/6 |

Only 100K steady gains beyond the floor. The b9bf610c run's 5M first-read gain beyond the floor
does not reproduce (main's pair 0 is a 1,428.5 us outlier). Page bytes unchanged from earlier
runs: varint 49.4 / 50.6 B per record against main 62.1 / 63.1 (-20%), fixed width 73.2 / 74.9
(+18%, +19%); the C++ builds equal the model byte for byte.

## Next

1. Suite job at the head passes: Evidence, State "ready for review".

## CHANGELOG entry (for the lead)

- One knob type: every knob is a signed 64-bit integer with a default, group overrides (a forced
  value or a percentage) and an optional rollout serving `rollout_value`; a flag is default 0,
  rollout value 1. `BoolDef`, `IntDef`, the JSON `type` field, `eval` and `eval_or_panic` are
  gone; reads are `get`, `get_as<T>` (`get_as<bool>` takes 0 and 1) and `get_or_panic`. A hashed
  caller outside a percentage gets `default` (D68). JSON, wire, paged body (varint, 20% fewer page
  bytes than the two-type body), flat image and reader metadata change with no compatibility.

## Blocked on

- Resolved: B5 (the lead added the S6 harness and observability.md to the set for deletion;
  deleted at Detent:7aed2593). Was: with docs/design/observability_bench.cc deleted, four files outside the set
  still name it: docs/design/observability-bench.sh (copies and builds it against S6),
  observability-summary.py and observability-paired.py (docstrings), docs/design/observability.md
  (:9, :89). Options: 1. the lead deletes the S6 bench harness (the .sh and both .py) and the two
  sentences in observability.md, or adds them to the set (recommended: they measure S6's C ABI,
  which is gone); 2. restore the .cc at main's text so the harness still builds against S6. The
  quest's acceptance criteria do not depend on it.
- Resolved: B4 (the lead added the four fixtures and converted them at Detent:eb51185d).
- Was B4 (file set): five test fixtures outside the set hand-encode main's integer definition
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
- Left alone as history: docs/reviews, docs/status, docs/handoffs, AUDIT.md, benchmarks/*.json,
  mutant *.patch files of landed quests (tests/detent_delivery/mutants/s3b-k2-*.patch names
  `try_eval` in context only and is not wired into a test).

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261111-200115-da0928; after the deletion and merge: SUITE_IDS |
| A2 | 261111-200115-da0928; fails without: 261111-183800-28bea7, 261111-183804-21b82d |
| A3 | 261111-201652-e744f8, 261111-201652-1b9d0e, 261111-201510-0027ae, 261111-201515-c9a550, 261111-201520-86effa, 261111-201525-efcbe6, 261111-201530-3605db, 261111-201535-254d6a, 261111-201539-7b4315, 261111-201545-be6229, 261111-201550-b4eb67, 261111-201555-ac7f70, 261111-201600-cbb858, 261111-201605-e35e36, 261111-201609-b99e4c, 261111-201613-6fb580, 261111-201618-c669dd |
| A4 | 261111-201651-a2bd67 |
| A5 | [artifact] docs/design/one-knob-type.md (definition, encodings with A3's numbers, evaluation, JSON, D65, D68); docs/design/knob-model.md rewritten for the single type |

## Result

Not yet.

## Rulings I made

- R11 The fixed-width arm is measured by the dense model only: the model equals main's C++ full
  build exactly (3,245 pages, 6,209,298 B on 100K), and the pages jobs check the varint C++ build
  against the model too. Speed is measured for the chosen body (varint) against main; the fixed
  arm loses by 18-19% page bytes before speed matters.
- R12 A1 records main's answers (one_knob_record.sh at `--ref BASELINE=main`) rather than keeping
  a frozen copy of main's evaluator: the recording is main's own code, the test reads its bits.
- R14 The dense graph fixture retries a packer or cursor commit refused for budget with a fresh
  `ValidationBudget`, as a real caller does; the refusal is the documented resumable path.
- R15 (superseded by the lead, session 4) observability_bench.cc called S6's C reader API, no
  longer in the tree; deleted under D43 (B5 lists the references outside the set).
- R16 The lazy bitmap fixture picks each namespace's definition by where its page landed, not by
  a fixed name: which cluster stays in the root page is the packer's size rule, not the test's.
- R17 A3's write-up cites the head run (Detent:dab00c10) only; the b9bf610c run stays a note in
  the perf doc, since its 5M first-read gain beyond the floor did not reproduce.
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
