# legacy-h: journal

State: waiting on jobs 261124-091549-10bfff 261124-091550-0e8703 261124-091550-ca58ab 261124-091551-8c7559 261124-091552-916f34 (reruns at `Detent:dceeb890`; round 3 APPROVE, correctness and design accepted)
Quest: .campaign/detent/quests/legacy-h.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Fix round 3 (review round 3 F5, CHANGES at `Detent:a9eb4f77`/`Detent:1417f2ad`; PLAN D154 last round;
D155 keeps reader.md D4) at `Detent:5cc302f8` (origin/main merged, no code change from it), pushed:
- Lifecycle audit: every transition of a namespace's entry, in
  `docs/design/client-local-source.md` D7 (copied below), checked against `agent_store.cc` and
  `core.cc`. No hole beyond F5's; F3's slot check is also covered by the kept generation
  watching its entry's slot since round 2, and its own test now proves it.
- F5 (`Detent:021dfa5e`): a dropped namespace watches the store word, not its old slot.
- D155 documented: guides/python-and-rust.md "When a Publish Can't Be Read"; the Python, Rust and C++
  references (only refresh reports a namespace that can't attach).
- F4 stays a follow-up (a dropped namespace's generation is held for the handle's life).

| Transition | The agent bumps | The lookup watches | The lookup answers | Test (`test_reader_core`) |
| --- | --- | --- | --- | --- |
| Generation published (the entry names a new generation) | the entry's slot, then the store word | the entry's slot | the new generation, from the lookup after the bump | `per_namespace` (`u` alone moves; `t` answers without a refresh) |
| Added (a namespace the table did not list) | grows `change`, then the new slot and the store word | before its first attach, the store word | UNAVAILABLE while the table read at the current store word omits it, then the generation; inside the agent's rename window too | `per_namespace` (`v`), `adding_namespace` |
| Attached (a first lookup) | nothing | the entry's slot, from the sequence loaded before the table | the entry's generation | `per_namespace` |
| Attach failed (an attached namespace's new generation cannot attach) | as published | the entry's slot, from the sequence it failed at | its last good generation with OK; the handle's other namespaces publish; `detent_refresh` returns the status; it retries when its slot moves or at `detent_refresh` | `entry_failure` |
| First attach failed | as added or published | the store word it failed at | the failure's status until the store word moves or a `detent_refresh` that does not fail | `refresh_memo`, `values` |
| Dropped (the table no longer lists it) | its old slot, then the store word | the store word: its old slot is no longer its own | its last generation with OK, held for the handle's life (follow-up F4) | `per_namespace` (`u` stays at 5) |
| Re-added at a new slot | grows `change`, then the new slot and the store word; never the old slot | the store word until it re-attaches, then the new slot | the re-added generation from the lookup after that publish, then each later publish | `regrow` (listed and unlisted handles, without `detent_refresh`) |
| Re-added with the same generation id at a new slot | as re-added | the new slot: a kept generation watches its entry's slot | the same generation, then the next publish in the new slot | `readd_same_generation` |
| Slot reused by another name | the reused slot and the store word | the new name, its entry's slot; the dropped name, the store word | the new name its generation; the dropped name its last one | `slot_reused` |

Tests (`test_reader_core`, by value): `regrow` (F5), `slot_reused`, `readd_same_generation` (F3), with
round 2's `adding_namespace`, `entry_failure`. Local: 9 of 9 targeted (core, files, scenarios,
syscalls, switch, generation writer, agent store, Python, Rust). Locally `regrow` fails at
`Detent:1417f2ad`'s core (line 208); `readd_same_generation` fails with the F3 mutant (line 278);
`slot_reused` passes at both (no fix: the old slot's bump refreshed).

## Next

1. When 261124-091549-10bfff, 091550-0e8703, 091550-ca58ab, 091551-8c7559, 091552-916f34 pass: State
   "ready for review" (all criteria cited above). Native 261124-075318-9b7397, bindings
   261124-075331-f4ad0a, fails-without 075309-3e8f55 and 075314-c9ce1c, CI 45430641919 and Docs
   45430641589 passed at `Detent:5cc302f8`.

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
| A1, A6-A8, A18, A19 | native 261124-075318-9b7397 |
| A2-A5 | 261124-075331-f4ad0a (`bindings/tests/run.sh`, repeat until-fail:3) |
| A9 | `ci:linux-asan-ubsan`, `ci:linux-tsan` (CI 45430641919 at `Detent:5cc302f8`) |
| A10 | 261124-091551-8c7559 (`bindings/tests/legacy_grep.sh`) |
| A11 | [artifact] `CHANGELOG.md`, "Legacy removal (wave 2, step H)" |
| A12 | 261124-075331-f4ad0a; fails without: 261123-145721-0d40d0, 261123-145724-5cb1a0, 261123-145728-9aba9a, 261123-145732-8cfaab, 261123-145735-aa6efc, 261123-145739-98e2a8 |
| A13 | 261124-091550-ca58ab |
| A14 | [artifact] `website/src/content/docs/reference/cpp-api/reader.md`: "share it; lookups are thread-safe on one reader."; client.md, sources.md, global-and-testing.md, guides/live-updates.md and diagram 06_adapters are deleted (A10's grep) |
| A15 | [decision] D13 |
| A16 | [decision] D14 |
| A17 | [decision] D21 (PIC build-wide); its `get` half superseded by D148 (A18) |
| A20 | 261124-075331-f4ad0a; fails without: F1 261123-145743-a3b9c5, F2 261123-145747-841eac, 261123-145751-a77953, F3 261123-145755-cd3bb8, 261123-145800-81ce67, F4 261123-145804-7bcb89, F7 261123-145808-ac5564 |
| A21 | native 261124-075318-9b7397; probe 261124-091549-10bfff; T2 261124-091550-0e8703; targeted 261124-091552-916f34; fails without: 261123-204320-f48488, 261124-004900-19d2cc, 261124-004904-f88615, 261124-075309-3e8f55, 261124-075314-c9ce1c |
| A22 | `ci:ci` (CI 45430641919), Docs 45430641589 at `Detent:5cc302f8` |

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
- Fix rounds 2 and 3: the agent grows `change` before `selected` names a slot (F1); a failed attach
  fails its namespace alone (F2, D155); a kept generation watches its entry's slot (F3); a dropped
  namespace watches the store word (F5). D7 in `docs/design/client-local-source.md` tables the
  entry lifecycle. Round 3 APPROVE (review `Detent:ff409c69`).
- Follow-ups, the lead's docs quest: review round 3 F6 (D7's four missing transitions and two wrong
  rules), F7 (pages outside this file set say slots are never reused), F8 (the D155 pages leave out
  refresh()'s OK while another thread refreshes), N2; and F4 (a dropped namespace's generation held
  for the handle's life).

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
    generation (as main's per-namespace Client did); a failed attach fails its namespace alone (fix
    round 2), a table or store failure the whole refresh (D4); `initial_slots` = 4,096 sequences loaded per refresh,
    raised when the table names a higher slot.
13. Writer builders live until their publication settles (`Detent:ae68d8f7`): splitting the plan
    out of publish had freed them first and let the old generation's segments go, which
    `test_testing_trusted_restart` refusals/t5 caught (A/B: old writer 3 of 3 pass, split 0 of 3,
    held 3 of 3).
