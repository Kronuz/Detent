# legacy-h: journal

State: waiting on jobs 261123-175720-145715 261123-175722-b26bcf 261123-175723-de3cb2 261123-175725-7fad98 261123-175727-3d4b80 261123-175759-67f119 and CI runs 45308791726 (CI), 45308791259 (Docs) at `Detent:8d61d548`
Quest: .campaign/detent/quests/legacy-h.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Fix round 1 after the correctness review (CHANGES at `Detent:e30799b8`), at `Detent:8d61d548`:
`Detent:407e4410` (per-namespace reader core, F1/F2, D150), `Detent:55b49db4` (Docs gate, F3, F7,
F11), `Detent:e1a45597` (F6, F8, F10), `Detent:ae68d8f7` (writer builder lifetime),
`Detent:8d61d548` (T2 benchmark). Pushed. Local (Release, macOS): reader, testing, bindings, CLI,
installed-component tests and `test_reader_switch` pass; `check_docs.py` "33 gaps, 33 listed, 0
unlisted, 0 stale"; `run_e2e.py --list` no MISSING.

## Next

1. Read the jobs: native gate 261123-175720-145715 (A1-A4, A6-A8, A18, A19, A21); bindings
   261123-175722-b26bcf (A2-A5, A12, A20, A21's Python/Rust); A10 grep 261123-175722-13a7f9 passed; A13
   261123-175723-de3cb2; review probe 261123-175725-7fad98 (A21: the website's pages on an agent store,
   `get` against main `Detent:6acfb0e9`); fails-without at the one-entry core 261123-175727-3d4b80
   (mutant `refs/mutants/legacy-h/a21-one-entry-core`, `Detent:43c6e350` = head + e30799b8's
   `src/detent_reader/core.cc`); T2 timing 261123-175759-67f119 (strict).
2. CI runs 45308791726, 45308791259 at `Detent:8d61d548`: `ci:linux-asan-ubsan`, `ci:linux-tsan`
   (A9), `ci:ci` and the Docs workflow (A22).
3. Evidence table from `campaign brief`; T2 numbers into Result and `docs/mechanisms/reader-core.md`;
   ready for review.

Done at `Detent:9ab80296`: native rerun 261123-141425-73c546 passed (A1-A4, A6-A8, A18, A19).

## Blocked on

Nothing blocks the work. msg-284 (to the lead): the fix round changes seven paths outside the file
set, which `campaign brief` flags: `src/detent_local/generation.cc`,
`src/detent_local/generation_file_access.hh` (the core's whole-table read and `select_entry`),
`tests/detent_testing/test_generation_writer.cc` (the C face writes an entry per namespace),
`tests/docs/check_docs.py`, `tests/docs/docs_exceptions.txt`, `tests/docs/e2e_exceptions.txt`,
`tests/docs/mutants.py` (A22). Recommend adding them. msg-266 and msg-267 closed earlier.

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
| A1, A2, A6-A8, A18, A19 | native 261123-141425-73c546 |
| A2-A5 | 261123-115749-3be19d (`bindings/tests/run.sh`, repeat until-fail:3) |
| A9 | `ci:linux-asan-ubsan`, `ci:linux-tsan` at `Detent:5610152f` |
| A10 | 261123-132715-96b85d (`bindings/tests/legacy_grep.sh`, passed at `Detent:62293129`) |
| A11 | [artifact] `CHANGELOG.md`, "Legacy removal (wave 2, step H)" |
| A12 | plain 261123-145542-ea7b40; fails-without 261123-145721-0d40d0, 261123-145724-5cb1a0, 261123-145728-9aba9a, 261123-145732-8cfaab, 261123-145735-aa6efc, 261123-145739-98e2a8 |
| A13 | 261123-115830-16b083 |
| A14 | [artifact] `website/src/content/docs/reference/cpp-api/reader.md`: "share it; lookups are thread-safe on one reader."; client.md, sources.md, global-and-testing.md, guides/live-updates.md and diagram 06_adapters are deleted (A10's grep) |
| A15 | [decision] D13 |
| A16 | [decision] D14 |
| A17 | [decision] D21 (PIC build-wide); its `get` half superseded by D148 (A18) |
| A20 | plain 261123-145542-ea7b40; fails-without F1 261123-145743-a3b9c5, F2 261123-145747-841eac, 261123-145751-a77953, F3 261123-145755-cd3bb8, 261123-145800-81ce67, F4 261123-145804-7bcb89, F7 261123-145808-ac5564; design doc updated |

## Result

Not yet.

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
