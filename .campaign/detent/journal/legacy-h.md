# legacy-h: journal

State: waiting on jobs 261123-132656-1c173d 261123-132712-88becb 261123-132715-96b85d 261123-132719-d878b9 261123-132722-52d35a 261123-132740-6858e4 and CI at `Detent:62293129`
Quest: .campaign/detent/quests/legacy-h.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Round 3 at `Detent:62293129` (fixes `Detent:1e7264e5`, `Detent:62293129`; main merged `Detent:72de6c74`).
CI run 45254351645 at `Detent:5610152f` failed on Linux only:
- `test_reader_exports`, `test_testing_exports` (every Linux job): `check_exports.cmake` runs as
  `cmake -P` without a minimum version; CMake 3.x leaves CMP0057 unset and rejects `IN_LIST`
  (CMake 4 locally makes it NEW). `test_reader_exports_unlisted` passed on that same error (WILL_FAIL);
  it now requires the "exports detent_close, which" message.
- `test_installed_testing_component` (asan, tsan): the consumer linked the sanitized libraries
  without the sanitizer runtime (`undefined reference to __tsan_unaligned_write16`); it now gets the
  build's `CMAKE_CXX_FLAGS[_<TYPE>]` and `CMAKE_EXE_LINKER_FLAGS`.
- `test_reader_switch` (linux-x86_64-gcc, T3 "threads 1 (before 2)"): the baseline counted the
  joined priming thread still in `/proc/self/task`; the 100 ms settle now follows the join.
Mutant f3-python built `int` into a `detent_status` member (exit 126); the patch casts it.
Local (Release, macOS): the three export tests, `test_installed_testing_component` and
`test_reader_switch` pass; a non-matching failure of the export script does not match the pattern.

## Next

1. Read jobs 261123-132656-1c173d 261123-132712-88becb 261123-132715-96b85d 261123-132719-d878b9 261123-132722-52d35a 261123-132740-6858e4 (native, bindings plain for A2-A5/A12/A20, A10 grep, A13, F3-python and F7
   mutants) and CI at `Detent:62293129` (`ci:linux-asan-ubsan`, `ci:linux-tsan`, `ci:ci`).
2. Evidence table from `campaign brief`; Result; ready for review.

## Blocked on

Nothing. msg-266 and msg-267 closed: the lead added `src/detent_local/evaluation.hh` (comment fixed)
and `include/detent_testing/**` (store.hh stays) to the file set.

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
| A1, A2, A6-A8, A18, A19 | native 261123-115748-542767 |
| A2-A5 | 261123-115749-3be19d (`bindings/tests/run.sh`, repeat until-fail:3) |
| A9 | `ci:linux-asan-ubsan`, `ci:linux-tsan` at `Detent:5610152f` |
| A10 | 261123-111526-9ed1e8 (`bindings/tests/legacy_grep.sh`) |
| A11 | [artifact] `CHANGELOG.md`, "Legacy removal (wave 2, step H)" |
| A12 | 261123-115751-5bff53, 261123-115753-f3287a, 261123-115755-5cd01d, 261123-115757-c99c71, 261123-115759-1641e1, 261123-115802-d5ab2f (fails-without) |
| A13 | 261123-115830-16b083 |
| A14 | [artifact] `website/src/content/docs/reference/cpp-api/reader.md`: "share it; lookups are thread-safe on one reader."; client.md, sources.md, global-and-testing.md, guides/live-updates.md and diagram 06_adapters are deleted (A10's grep) |
| A15 | [decision] D13 |
| A16 | [decision] D14 |
| A17 | [decision] D21 (PIC build-wide); its `get` half superseded by D148 (A18) |
| A20 | 261123-115804-bf7616 (F1), 261123-115806-c752e0, 261123-115808-e4edc7 (F2), 261123-115809-d73ef0, 261123-115811-ea984a (F3), 261123-115815-a432cd (F4, c3), 261123-115813-9d3c88 (F7) (fails-without); design doc updated |

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
