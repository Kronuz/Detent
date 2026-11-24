# legacy-h: journal

State: waiting on jobs 261123-215328-7c1230 261123-215333-3427d0 261123-215204-e4d157 at `Detent:a9eb4f77` (Evidence and Result written; ready for review once they pass)
Quest: .campaign/detent/quests/legacy-h.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Fix round 1, second pass, at `Detent:109f51ae` (main merged `Detent:57cbd641`, pushed).
Results at `Detent:8d61d548` (the lead's msg): review probe 261123-175725-7fad98 passed, T2
261123-175759-67f119 passed, fails-without 261123-175727-3d4b80 failed as required, A10 grep
261123-175722-13a7f9 passed, Docs 45308791259 green. Failures and fixes:
- bindings 261123-175722-b26bcf and CI `bindings-*`: `detent get` exit 127; `test_reader_scenarios`
  now depends on `detent-reader` (`Detent:109f51ae`). `detent_python_test` timed out at 120 s at
  load ~32 (22 s at light load); bound 300 s.
- CI `ci / macos-x86_64`: `test_reader_switch` parks (`seen == pinned_rev`): when the refusals'
  backoff ran out before the released reader resumed, its stale lookup refreshed first and never read
  its pinned view. `Detent:dec0d978` (lead's correction of `Detent:109f51ae`'s either-value check):
  testing knob `hold_retry` closes the retry clock until the next publish; the case sleeps 250 ms
  before release and checks `seen == pinned_rev` exactly. Without the knob 5 of 5 fail there; with it
  30 of 30 alone and 90 of 90 six-way parallel pass. `test_authority_frames_service` (409
  session_busy) is not this quest's.
- native 261123-175720-145715: `test_reader_switch`, `test_agent_store_crash` timed out at load
  ~32; rerun alone 261123-200502-4fbd30. `test_relay_service` is the lead's (port-reuse A/B).
- A13 261123-175723-de3cb2 hit 30 min building: split, build under `--build`, 261123-200500-f13c52.
Local after the merge: reader, testing, bindings, CLI, installed tests pass (26 of 26);
`check_docs.py` 0 unlisted, 0 stale; `run_e2e.py --list` no MISSING.

## Next

1. Read native 261123-215328-7c1230 (A1, A6-A8, A18, A19, A21) at `Detent:a9eb4f77`; on a pass, State
   "ready for review". A10 grep 261123-215330-5b6a71 passed there.
2. `test_authority_snapshot_catchup` (native 261123-204256-3ca620, failed once, 16.6 s, no detail; this
   branch touches neither `src/detent_authority` nor `tests/detent_authority`): 10 runs alone at the
   head 261123-215333-3427d0 and at main `Detent:5b40d3c8` 261123-215204-e4d157. If main fails too, it
   is pre-existing (the lead files it); if only the head fails, find why.

Done at `Detent:dec0d978`: bindings, probe, T2, A13, switch and store-crash alone passed;
fails-without 261123-204320-f48488 failed as required; CI 45328288209 and Docs 45328287755 green.

## Blocked on

Nothing. msg-284 answered: the lead added the seven paths to the file set.

## Correctness review (legacy-h-correctness.md) findings

- F1, F2 (A21, D150): the core reads the whole `selected` table once per refresh (sequences of the
  slots it names loaded first, D4 rule 1; retried on a rename; slots past 4,096 raise the bound and
  reread), attaches a namespace's entry on its first lookup, and keeps attached entries sorted for a
  binary search. Each entry follows its own sequence; a namespace that leaves the table keeps its
  last generation; one the table never listed answers UNAVAILABLE. Tests: `test_reader_core`
  `per_namespace` (two entries, a list-opened handle, isolation of a delta in `u` from `t`, a new
  namespace attaching, a dropped one staying stale, 64 namespaces read with one `selected` read and at
  most 4 views); `test_reader_scenarios` runs `detent get` on 21 rows over the five-entry scenario
  store; the bindings read the same store. Fails without: 261123-175727-3d4b80.
- F3 (A22): `check_docs.py` reads the statuses from `reader.h`; three stale flag exceptions go;
  e2e excepts `guides/cpp-application#1` (a full project build) and drops `shell-and-agent#6`.
- F4: the Docs workflow is now read with CI at each push (Next 2).
- F5: CHANGELOG says `DETENT_DEFAULTS_PATH` is ignored.
- F6: `detent get`'s usage says "(default 512 MiB)".
- F7: the `detent` reference names `DETENT_READER_MAPPING_BYTES`.
- F8: Python copies the namespace list into a tuple before releasing the GIL.
- F9: `Store` states its thread rule; publish on a moved-from `Store` throws `std::logic_error`
  (`tests/cmake_consumer/installed`).
- F10: the testing shared library and its install exist only with libdetent_reader's shared twin.
- F11: mutant M3 renames `generation_dir` in a tree carrying the sources; A10's grep covers
  `mutants.py` again.
- F14: reader-core.md, client-local-source.md, update-path.md and how-it-works.md describe the
  per-namespace table.
- Not taken: F12 (the grep's exclusions are rulings 6 and 9; the lead's to restate A10), F13 (T5
  against the old binary is S7's; the scenarios' `detent get` rows cover values), F15 (A2's count is
  the quest file's).

## Design review (legacy-h-design.md) findings

- F1 (blocking): both bindings, the C++ runner and `scenarios.json` are on the landed ABI: one
  `detent_get` with a fallback; `get`, typed `get_as` (Rust `get_as<T: Knob>`, Python `get_bool`),
  `lookup(name, fallback, ...)`; no `eval`, kind or `WRONG_TYPE`. Table: 56 rows (41 before), with
  typed out-of-range rows (flag read of 2, `int8` of `INT64_MAX`, `uint64` of `INT64_MIN`) and an
  integer with rollout and group overrides. Test: the rows in all three runners.
- F2 (blocking): `mapping_bytes` in both bindings; T2 checks a cap of 1 raises INVALID_ARGUMENT and
  the minimum opens, in each language.
- F3 (blocking): Python's `Status` takes its values from `reader.h` (compiled in); a Rust unit test
  checks `from_raw(n).name()` against `detent_status_name(n)` for every known value. T2's UNTRUSTED
  checks fail on the old numbering.
- F4 (blocking): `test_one_knob_differential` runs the paged evaluator over the parsed definition
  (no Client); `one_knob_record.cc` stays (it builds only against the commit it records); c3
  retargeted to `reader.hh`'s `get_as<bool>` (killed by `test_reader_core`'s "5 is not a flag").
- F7: each shared library's `exports.list` names its functions; CMake writes the version script and
  the Apple list from it; the check compares sets; `test_reader_exports_unlisted` (WILL_FAIL) proves
  an unlisted export fails it.
- F8: CHANGELOG names the removed capabilities.
- F9: `Py_MOD_GIL_NOT_USED` dropped until a free-threaded run passes (follow-up).
- F11: T2 lookup-time UNTRUSTED row in Python and Rust.
- F10 (PIC build-wide unmeasured), F12 (macOS bindings job toolchain): not taken; follow-ups.
- Design doc restated on the landed ABI (`docs/design/client-local-source.md`).
- F5 ruled D148 (A18): `detent get` opens `detent::Reader` (no `GenerationReader` in `get`), takes
  `--reader-mapping-bytes` (flag, `DETENT_READER_MAPPING_BYTES`, `reader_mapping_bytes`), refuses the
  other nine `--reader-*` flags and `--reader-validation full`; errors print `ERR <status>: ns:key`.
  Tests: `test_agent_cli` (mapping cap 1 refused, minimum opens; each refused flag by name;
  `full` refused, `prepared` accepted) and `test_managed_process` (flag over variable over config).
- F6 ruled D149 (A19): `DETENT_INSTALL_TESTING` builds both shared libraries; `cmake --install
  --component development` (EXCLUDE_FROM_ALL) installs them, `reader.h`, `reader.hh`,
  `generation_writer.h`, `store.hh` and `DetentConfig.cmake` (`detent::reader`, `detent::testing`).
  `test_installed_testing_component` installs into the build tree, builds
  `tests/cmake_consumer/installed` with `find_package(Detent)`, publishes, reads, republishes, refreshes.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.

| Criterion | Evidence |
|---|---|
| A1, A6-A8, A18, A19 | native 261123-215328-7c1230; A1's switch and store-crash alone 261123-204300-e4fd2c |
| A2-A5 | 261123-204301-a7a0cc (`bindings/tests/run.sh`, repeat until-fail:3) |
| A9 | `ci:linux-asan-ubsan`, `ci:linux-tsan` (CI 45328288209 at `Detent:dec0d978`) |
| A10 | 261123-215330-5b6a71 (`bindings/tests/legacy_grep.sh`) |
| A11 | [artifact] `CHANGELOG.md`, "Legacy removal (wave 2, step H)" |
| A12 | 261123-204301-a7a0cc; fails without: 261123-145721-0d40d0, 261123-145724-5cb1a0, 261123-145728-9aba9a, 261123-145732-8cfaab, 261123-145735-aa6efc, 261123-145739-98e2a8 |
| A13 | 261123-204301-67a905 |
| A14 | [artifact] `website/src/content/docs/reference/cpp-api/reader.md`: "share it; lookups are thread-safe on one reader."; client.md, sources.md, global-and-testing.md, guides/live-updates.md and diagram 06_adapters are deleted (A10's grep) |
| A15 | [decision] D13 |
| A16 | [decision] D14 |
| A17 | [decision] D21 (PIC build-wide); its `get` half superseded by D148 (A18) |
| A20 | 261123-204301-a7a0cc; fails without: F1 261123-145743-a3b9c5, F2 261123-145747-841eac, 261123-145751-a77953, F3 261123-145755-cd3bb8, 261123-145800-81ce67, F4 261123-145804-7bcb89, F7 261123-145808-ac5564 |
| A21 | native 261123-215328-7c1230; probe 261123-204301-46672f; T2 261123-204301-53e01c; fails without: 261123-204320-f48488 |
| A22 | `ci:ci` (CI 45328288209), Docs 45328287755 at `Detent:dec0d978` |

## Result

Step H removes the Client and its sources, in C++, Python and Rust. The bindings and `detent get`
read through the reader core's C ABI (`detent_get`, `mapping_bytes`, `Status` taken from `reader.h`).
Fix round 1 (correctness review F1-F11, F14):
- The reader core reads the per-namespace table (D150, `Detent:407e4410`). It reads `selected` once
  per refresh and attaches a namespace on its first lookup. Each namespace follows its own sequence.
  A 64-namespace store opens with one table read and at most 4 views. `detent get`, Python and Rust
  read the five-entry scenario store, and `get` matches main's on the website's agent-store pages
  (probe). Lookup cost is measured in `docs/mechanisms/reader-core.md` (job
  261123-204301-53e01c, strict, 20,000 knob pairs): hot p50 958 ns with 1 namespace and 1,017 ns
  with 1,000 attached; scattered 1,503 and 1,280 ns; 0 allocations.
- Docs gate (A22): `check_docs.py` 33 gaps, 33 listed, 0 stale; e2e has no MISSING block.
- Tests: `test_reader_core` per_namespace; `test_reader_scenarios` runs `detent get` on 21 rows;
  `test_reader_switch` parks is deterministic through `hold_retry` (without it the check fails 5 of
  5); the testing writer's per-namespace layout; `benchmarks/reader_namespaces.sh`.
- Docs written: `docs/mechanisms/reader-core.md`, `docs/design/client-local-source.md`, the
  website's reference, update-path and how-it-works pages, `CHANGELOG.md`.

## Rulings I made

1. Merged main (Detent:88ac01cd) into the quest branch, which started at `legacy/wave2-h`
   (Detent:aac6cb6b): `Detent:ce2dd309`. Deletions kept; main's later edits to deleted files dropped.
2. `detent get` (D148): open failures (a cap below the minimum, an untrusted store) are hard errors
   even with `--default`; `--default` answers MISSING, UNAVAILABLE and the other lookup statuses but
   not SOURCE_FAILURE or UNTRUSTED. `get` ignores the other `reader_*` settings in `agent.json` and the
   environment (they configure `--status`), refusing only the flags. `test_managed_process` now names
   the test user as `publisher_user` instead of `reader_validation: "full"`, since `get --system`
   trusts the service account by default.
3. Typed reads: Rust `get_as<T: Knob>` (sealed: `bool`, `i8`..`u64`) mirrors C++ `get_as<T>`;
   Python keeps one explicit `get_bool`, its ints having no width. Python skips the non-bool typed rows.
4. `include/detent/detail/evaluation.hh` keeps `detail::ramp` (the one percentage rule the paged
   evaluator uses); only `detail::evaluate` goes.
5. `prepared_client_probe` no longer prints `records` (the Reader has no count);
   `dense_reader_measure.py` no longer checks it.
6. A10's grep excludes historical records: `benchmarks/*.json`, `benchmarks/container-alternative/*.patch`
   (applied to deleted files of a past experiment) and `tests/detent/one_knob_record.cc`.
7. Session: a subagent of the lead's session, not a `legacy-h` titled session.
8. A19's C++ face is `detent::testing::Store`, header-only over the C face like `Reader` over
   `reader.h`; `GenerationWriter` stays the implementation behind the C face and Detent's own tests'
   writer (delta, compaction, repair). In-tree alias `detent::testing` is the same shared library
   the package exports, so in-tree and installed consumers name one target.
9. A10's grep (`bindings/tests/legacy_grep.sh`) also excludes dated records (`docs/dev/docs-site-audit.md`,
   `DELIVERY_INTEGRATION_ARCHITECTURE`, `READER_CAPACITY_ARCHITECTURE`, `CONTAINER_SELECTION_*`,
   `DOCUMENTATION_FINDINGS`, `benchmarks/container-alternative/`, `docs/design`, `docs/handoffs`) and
   the refusal tests (`test_agent_cli.cmake`, `tests/docs/mutants.py`). `SCALE_TARGETS.md` item 1 and
   both how-it-works status rows are reworded.
10. `test_reader_switch`'s baseline race is in a reader-core test this quest does not otherwise
    touch (likely pre-existing on main: a timing race, not shown by a job at main); fixed inside the
    file set (`tests/detent_reader/**`) since it red-lit this quest's CI.
11. The test writer has two layouts (`GenerationWriter::Layout`): `OneEntry` (default; store-level
    tests outside the file set keep their one-entry stores) and `PerNamespace` (an entry per
    namespace with its own cursor scope, deltas moving one entry, at most 16 plans per cycle; compact
    is OneEntry-only). The C face and `detent::testing::Store` use `PerNamespace`, as an agent does.
12. Reader-core semantics for D150: a namespace that leaves the table keeps answering from its last
    generation (as main's per-namespace Client did); a refresh failure on one entry fails the whole
    refresh and keeps the installed view (D4); `initial_slots` = 4,096 sequences loaded per refresh,
    raised when the table names a higher slot.
13. Writer builders live until their publication settles (`Detent:ae68d8f7`): splitting the plan
    out of publish had freed them first and let the old generation's segments go, which
    `test_testing_trusted_restart` refusals/t5 caught (A/B: old writer 3 of 3 pass, split 0 of 3,
    held 3 of 3).
