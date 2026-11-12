# one-knob-type: journal

State: waiting on jobs at Detent:432d75a4 (code final at 52afffdc): check 261111-183335-cadb42,
suite 261111-183749-ea6a83, docs e2e 261111-183751-b78a79, A4 261111-183756-114e28, A2 mutants
261111-183800-28bea7 / 261111-183804-21b82d, A3 prepare 261111-183845-6f9674, pairs 100k
261111-183845-21dfff .. 261111-183845-8adf69, 5m 261111-183846-bdf446 .. 261111-183846-29ad06,
pages 261111-183846-09f47c (100k) / 261111-183847-4a0ddc (5m), reports 261111-183858-1f07ad (100k)
/ 261111-183901-4ac6d4 (5m)
Quest: .campaign/detent/quests/one-knob-type.md (the lead's; never edit it)

## Doing

Session 3. Merged main (ipv6, relay-delta-shutdown-hang, brand) at Detent:83bda97c; both
scripted_upstream.hh changes kept (auto-merged). Suite 261111-163109-4e98df at eb51185d, 6 failed:

- test_local_lazy_page_bitmap: `dense_graph_fixture.hh:88` `check(packer.commit(...))`; commit
  returns false when the step before spent the validation budget (8,192 B, 16 rows), which the
  smaller varint bodies now hit. The fixture retries packer and cursor commits with a fresh
  budget (R14). Detent:f6c62a85.
- test_local_dense_reads:422 assumed a 24-byte body; it now flips byte 10, the default's one-byte
  varint. Detent:f6c62a85.
- test_authority_snapshot_catchup: TimeoutError at test_multivoter.py:304, the 98-write loop with a
  voter down, beside test_local_disk_store's 272 s. Pre-existing flake: the same line failed in
  261109-085306-1a0b81 (namespace-s1-s2), 261109-162029-7d42a7 (daemon-ready-leader) and
  261111-013718-1582cb (dense-walkers), and passed on this branch in 261111-155807-cb02cd and
  261111-162230-a8bbcb.
- test_authority_dns_voters_names: "bind: Address already in use"; main's ipv6 merge changes
  test_dns_voters.py's port reservation. Rerun decides.
- test_agent_store_crash, test_local_publication_power_cut (Timeout 900 s): the flake relay-delta
  saw; rerun decides.

Owner ruling (one varint): varint pinned at 333384ee, in the core FetchContent list, linked PUBLIC
to detent_local_codec; `zigzag`/`varint_size` and the hand-rolled LEB128 are gone. Detent:f6c62a85.
observability_bench.cc on one type (Detent:63418832). A4's script used python3.14 for CMake while
pybind11 built for 3.15 (job 261111-162758-63e456: ModuleNotFoundError); one interpreter now
(Detent:aaca44bb). A3 read speed in both notes (Detent:52afffdc); b9bf610c's pair outputs copied to
/Users/kronuz/code/Kronuz/Detent/.scratch/okt/b9bf610c/ before the head reruns overwrite them.

## Next

1. 261111-183335-cadb42 green, then at the head: `campaign verify one-knob-type --for A1,A2`, docs
   e2e, A4, both A2 mutants, A3 `campaign job rerun` prepare + 12 pairs + pages 100k/5m, then the
   two reports.
2. Fill Evidence from `campaign brief`, Result, State "ready for review"; push.

## CHANGELOG entry (for the lead)

- One knob type: every knob is a signed 64-bit integer with a default, group overrides (a forced
  value or a percentage) and an optional rollout serving `rollout_value`; a flag is default 0,
  rollout value 1. `BoolDef`, `IntDef`, the JSON `type` field, `eval` and `eval_or_panic` are
  gone; reads are `get`, `get_as<T>` (`get_as<bool>` takes 0 and 1) and `get_or_panic`. A hashed
  caller outside a percentage gets `default` (D68). JSON, wire, paged body (varint, 20% fewer page
  bytes than the two-type body), flat image and reader metadata change with no compatibility.

## Blocked on

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
- R14 The dense graph fixture retries a packer or cursor commit refused for budget with a fresh
  `ValidationBudget`, as a real caller does; the refusal is the documented resumable path.
- R15 observability_bench.cc still calls S6's C reader API (`detent/reader.h`, no longer in the
  tree), so it is converted in shape and not built: one `lookup_get`, one-type definitions, no
  `--ops`; its output keeps `"op":"get"` for the summary scripts outside the set.
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
