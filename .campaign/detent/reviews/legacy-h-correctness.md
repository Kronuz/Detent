# legacy-h review: correctness

Commit: Detent:e30799b83d86603e95b46418a38b685582bc7f86 (`quest/detent/legacy-h`; its successor
Detent:dbbbf985 changes only the journal). Diffed against main's Detent:6c945801; merge base
Detent:18ce371d.
Verdict: CHANGES
Date: 2026-11-23

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

The bindings, the export lists, the development component and `detent get`'s option set are
right, and the design review's blocking F1-F4 and F7 are fixed in the code, not only in the
mutants. What breaks is the read path D148 moved `detent get` onto: the reader core opens only a
store whose table holds one entry, and the agent writes one entry per namespace. `detent get`
therefore fails with `detent_open: source_failure` as soon as a second namespace has knobs, which
the website's own pages do (the Docs gate is red at this head, green on main), and with
`ClientSourceOptions::name_space` deleted no C++, Python or Rust API reads such a store (F1, F2).
Neither fix is a reviewer's call: the core learns per-namespace entries in or before this quest,
or the owner amends D148 and D14.

## Evidence I relied on

- Native 261123-141425-73c546 at Detent:9ab80296 (the head's code; Detent:e30799b8 adds only the
  journal): full configure with authority, relay, examples, benchmarks, tests, Python and Rust;
  218 of 218 tests pass, `test_local_trusted_paths` skipped. A1-A8 on macOS.
- CI 45269732772 at Detent:62293129: `bindings-ubuntu-24.04` and `bindings-macos-15` (the scenario,
  export and binding tests under `--repeat until-fail:3`), Linux GCC and Clang, arm64, ASan/UBSan,
  TSan and macos-arm64 green; macos-x86_64 cancelled. At the head, CI 45284336540 has every job
  green (both `bindings` jobs, Linux GCC, Clang and arm64, ASan/UBSan, TSan, macos-arm64) but
  macos-x86_64, still running when I wrote this. Linux export checks and the switch test's primer
  fix on Linux rest on these runs.
- The lead's 13 mutant jobs (261123-145721-0d40d0 to 261123-145808-ac5564), each at a commit on the
  head changing one file: all killed (exit 8 under `fails_without`). The four logs I read fail the
  named test: c3 (Detent:656ec78e, `reader.hh` only) aborts `test_reader_core`, f7 fails
  `test_reader_exports_unlisted`, f3-rust fails `statuses_match_the_header`, f2-python fails
  `detent_python_test`.
- Docs 45284334562 at the head: `check-docs` and `e2e` fail (F1, F3). Main's Docs 45280572341 at
  Detent:afa986b0, a descendant of the merge base, is green.

## What I checked myself

Scripts under `.scratch/detent/review-legacy-h/` in the home (rerun on a fix):

- `review_bindings.sh` at the head, job 261123-151832-28740a (its Python and CLI probes did not run:
  the script read the wrong cache variable for the interpreter) and its rerun 261123-160814-6f0c98:
  - A1-A5 again: `test_reader_scenarios`, `test_reader_exports`, `test_reader_exports_unlisted`,
    `test_testing_exports`, `detent_python_test` and `detent_rust_test` under `ctest --repeat
    until-fail:3` pass. A18, A19 and the ports: `test_agent_cli`, `test_managed_delivery_process`,
    `test_installed_testing_component`, `test_one_knob_differential`, `test_local_lazy_page_access`,
    `test_local_evaluation` and `test_reader_core` pass.
  - Risk 5: `test_reader_switch switch` passes 5 of 5 short runs (3 s, 150 publishes). The primer fix
    (`tests/detent_reader/test_reader_switch.cc:121-126,236-243`) is bounded rather than timing-free: it
    waits up to 5 s for the joined primer to leave the thread list and fails, never passes, if it
    does not.
  - Risk 4: a plain `cmake --install` installs the three binaries and the service files, no library,
    header or CMake package; `--component development` installs exactly `reader.h`, `reader.hh`,
    `generation_writer.h`, `store.hh`, `DetentConfig*.cmake`, `libdetent_reader` and
    `libdetent_testing`, no binary. With tests, bindings and `DETENT_INSTALL_TESTING` off the
    component is empty; with the option on it has rules. The installed libraries export exactly
    their lists (`check_exports.cmake` on the installed files: 7 and 5 functions), with `@rpath`
    install names and no build-tree path. `store_probe.cc`, built against the installed component
    with `find_package(Detent)`: invalid JSON and a name without a namespace throw
    `std::runtime_error` naming the call, a later publish still works, a moved `Store` publishes the
    next revision, a `Reader` outlives the `Store`, a `Store` over a regular file throws, a second
    `Store` on one directory is refused by the journal lock ("lock journal owner: Resource
    temporarily unavailable"), missing parents are created, and `publish` on a moved-from `Store`
    dies with SIGSEGV (F9).
  - Risk 2, Rust: `review_fork.rs` (copied into `bindings/rust/tests`): a forked child's handle sees
    the parent's next publish and refreshes; `lookup` results outlive their `Reader`. Both pass.
  - A6: `tests/cmake_consumer` configures, builds and passes against the head.
  - Risk 2, Python (`py_probes.py`): the ten `Status` members carry `reader.h`'s numbers and the
    library's names (UNTRUSTED 6); a `Lookup`, a missing `Lookup` and `refresh`'s status outlive their
    `Reader`, and a bound method keeps the handle; 16 threads against 300 publishes see only OK and
    never go backwards; 40 forks while two threads look up, one refreshes and one publishes (19
    publishes during them): each child reads a value no older than the parent's, refreshes (OK or
    UNAVAILABLE) and looks up again, never older. All pass.
  - Risk 3 (`cli_probe.py`): each of the nine other `--reader-*` flags is refused by name before any
    read, `--reader-validation full` is refused and `prepared` accepted, `--defaults` is an unknown
    argument; `--reader-mapping-bytes 1055136` reads, and 1, by flag or by
    `DETENT_READER_MAPPING_BYTES`, reaches the core (`detent_open: invalid_argument`), the flag
    winning over the variable; 20 rows of `scenarios.json` through `detent get` print the table's
    value, `ERR <status>: <name>` with exit 1, or the `--default` fallback; `detent-agent --status`
    takes all ten `--reader-*` flags and `--reader-validation full`. `DETENT_READER_RECORDS=1` and an
    `agent.json` with `reader_validation: full` do not affect `get`. The job's one FAIL row is the
    probe's: `get other:hidden` expects the fallback from a handle limited to the table's four
    namespaces, and `get` opens the name's own; `cli_probe.py` skips that row since the run.
    `--status` on the test writer's store lists one entry (`namespace=namespace`, 12 knobs across
    five namespaces), where the agent's store in the e2e job lists three.
- `review_e2e.sh`, job 261123-151840-af4466 at the head with `MAIN=Detent:6c945801` (exit 1, the
  finding): builds the head's authority, agent and `detent` and main's `detent`, runs the website's
  quickstart, settings-and-flags, rollouts and group-overrides pages (`tests/docs/run_e2e.py
  --keep`), then reads the store they leave with both binaries. 10 of 21 blocks fail, each with
  `detent: detent_open: source_failure`, from settings-and-flags#2 on, the first block after a
  second namespace gets a knob; plus the two page errors of F3. `detent-agent --status` lists three
  table entries (checkout revision 2, payments 6, search 5). 13 of 15 reads differ: the head exits 1
  with `detent_open: source_failure` on every read, `--default` included; main prints the pages'
  values (`search:max_results` 500, `checkout:new_path` 0, `payments:new_processor --hash user-42`
  1, the four group switches 1/0/1/0, `--default 7` and `--default 3` fallbacks). The other two
  fail on both (main: `ERR MissingKnob` for a deleted knob, a descriptor error for a name without
  a namespace).
- The design review's blocking fixes, in the code: F1, the bindings sit on today's ABI (`get`,
  `get_bool`/`get_as`, `lookup -> (status, int64)`, no `eval` or kind; `bindings/rust/src/lib.rs`,
  `bindings/python/detent_python.cc`), and `scenarios.json` has no `eval` row. F2, `mapping_bytes`
  reaches `detent_open_options` in both (`detent_python.cc:130-195`, `lib.rs:194-214`), and T2
  opens at 1 (INVALID_ARGUMENT) and at 1,055,136. F3, Python builds `Status` from the compiled
  `DETENT_*` constants (`detent_python.cc:64-77`), and Rust's unit test compares every raw value's
  name with the linked `detent_status_name` plus `Status::Untrusted.name() == "untrusted"`
  (`lib.rs:300-311`): test time against the library, as F3 asked. F4, the differential now checks
  the paged evaluator against main's recorded answers (`tests/detent/test_one_knob_differential.cc`),
  not `Reader`, and c3 patches `reader.hh`'s `get_as<bool>`. F7, both lists name exact functions
  (`src/detent_reader/exports.list`, seven; `src/detent_testing/exports.list`, five) and
  `check_exports.cmake` compares sets.
- Risk 1, the capabilities of the deleted API: value, typed, hashed and switched reads moved to
  `Reader`; `health()`, `Metrics`, `get_or_panic`, `ConversionFailed`, the staleness alert,
  `on_update`, `max_knobs`, enumeration and `detent::global` go under D14 and are named in the
  CHANGELOG; `TestKnobs`/`TestGuard` go to `libdetent_testing` (D149); `--defaults`,
  `defaults_path` and `--reader-validation full` for `get` go under D148. One capability has no
  replacement: reading one namespace's entry of a multi-entry store (F2).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|----|----------|------|---------|----------|-----|
| F1 | yes | defect | `detent get` fails every read of an agent store holding more than one namespace's entry, the state the website's pages reach. On the store four pages leave (entries checkout, payments, search) the head's `get` exits 1 with `detent: detent_open: source_failure` for all 15 reads, `--default` included, where main's prints the values for 13 of them. D148 put `get` on `detent::Reader`; its core selects the table entry with no name (`src/detent_reader/core.cc:439`, `PreparedReader::selected(dir, publisher)`), `read_selected(..., "")` bounds the table at one entry (`src/detent_local/generation.cc:178`) and `read_table` throws past it (`:116`), which the core reports as SOURCE_FAILURE. Main's `get` read the namespace's own entry (`local.name_space = prefix`, `src/detent_agent/main.cc:232` at Detent:18ce371d). The core is unchanged by this quest; moving `get` onto it is what breaks. The suite cannot see it: `get` is tested on a missing store (`tests/detent_agent/test_agent_cli.cmake:56-66`) and a one-namespace agent (`test_managed_process.sh`, `["ns"]`), and the bindings on the test writer's store, one entry holding every namespace | Probe job 261123-151840-af4466 (10 of 21 e2e blocks, 13 of 15 A/B reads, three entries in `--status`); probe 261123-160814-6f0c98 (`--status` on the test writer's store: one entry holding five namespaces); Docs run 45284334562 `e2e` (the same 10 blocks) | Restore per-namespace reads; the route is the owner's, since D148 fixed `get`'s reader. (a) The core attaches the entry the looked-up namespace names and watches that entry's `change` slot (`docs/mechanisms/reader-core.md:23` foresees `find` by name), in this quest or in a reader-core quest that lands first. (b) The owner amends D148: `get` keeps main's per-namespace read until (a) lands. Test: `detent get` by value over a store with one entry per namespace (the e2e pages, and a test-writer mode that publishes per-namespace entries as the agent does), and the same store through `Reader` in C++, Python and Rust |
| F2 | yes | gap | No remaining API reads a store with more than one entry, and no ruling says so. Main's `ClientSourceOptions::name_space` ("The namespace whose table entry the source reads (D8)", `include/detent_local/client_source.hh:14-15` at Detent:18ce371d) read one namespace of such a store. The design maps `make_local_read_source(opts)` to `detent::Reader(dir, uid, namespaces)` (`docs/design/client-local-source.md:83`) on the premise that the store has one table entry (`:136`), which no longer holds: the agent writes an entry per namespace (`src/detent_local/disk_store.cc:330`, `agent_store.cc:447-459`). After this quest an application whose agent follows two namespaces with knobs gets SOURCE_FAILURE from opening a `Reader` in C++, Python or Rust (the open that fails in F1), while the guides describe per-namespace reads (`website/src/content/docs/guides/cpp-application.md`, "the agent hasn't published this namespace yet"; `website/src/content/docs/reference/cpp-api/reader.md:88`). D14 rules out the Client, not this capability, and no open quest owns per-entry reads in the core (D94's "per-namespace reader attach" sat in the dropped namespace-s6-v1-removal) | Code as cited; probe 261123-151840-af4466 (the same `detent_open`) | F1's (a) settles both. Landing before it needs an owner ruling that `Reader` reads only single-entry stores until (a), with the guides and CHANGELOG saying so: the owner's call, since it narrows what applications can read |
| F3 | yes | defect | The rest of the Docs gate is red at the head, from this quest's page rewrites. `check-docs` fails ("51 gaps, 37 listed, 17 unlisted, 3 stale"): `tests/docs/check_docs.py:167-180` still takes the error names from `errors.hh`'s `ErrorCategory`/`UpdateRejectReason` (Checksum, MissingKnob, Oversize, Parse, Schema, Uninitialized now source-only) while `errors.md` lists the reader statuses (OK ... UNTRUSTED docs-only), `--reader` is a docs-only flag, and `tests/docs/docs_exceptions.txt:28,32,40` (`--publisher-user`, `--reader-mapping-bytes`, `--reader-validation`) are stale. `e2e` has two page errors F1's fix leaves: `guides/cpp-application#1`, the rewritten page's build-and-run block, has no assertion block and no exception, and `guides/shell-and-agent#6` is excepted but gone | Docs run 45284334562 (`check-docs` log, `e2e` ERROR lines); probe 261123-151840-af4466 (the same two page errors) | Teach `check_docs.py` the reader statuses (from `reader.h`) and the error names still shown to users, drop the three stale exceptions, give `cpp-application#1` an assertion block or an exception with its reason, and remove `shell-and-agent#6`'s. Test: the Docs workflow green |
| F4 | no | gap | The journal never mentions the Docs workflow, red at every pushed head since Detent:5610152f (runs 45254350988, 45269732283, 45284334562), while its CI line reads "every job green except macos-x86_64"; the quest's gate is CI green on the merged head | `gh run list -R Kronuz/Detent --branch quest/detent/legacy-h` | Record the Docs runs in the journal's CI rows |
| F5 | no | nit | The CHANGELOG says `DETENT_DEFAULTS_PATH` is "rejected"; nothing reads it any more (`git grep DETENT_DEFAULTS_PATH -- src` is empty), so it is ignored. With it set to a missing file, `get` reads 25 and exits 0 | `CHANGELOG.md`, "Legacy removal (wave 2, step H)"; probe 261123-160814-6f0c98 | Say "ignored", or refuse it through the removed-option table (`src/detent_cli/config.cc:111-125`, today the authority's only) |
| F6 | no | nit | `detent get`'s usage says `--reader-mapping-bytes N` caps the mapped bytes "(0: 512 MiB)" (`src/detent_agent/main.cc:286-287`), but 0 is refused as "invalid reader capacity: mapping-bytes" (`:219-220`). `--reader-mapping-bytes 0` exits 1 with that message | Code as cited; probe 261123-160814-6f0c98 | Drop "(0: 512 MiB)", or take 0 as the default |
| F7 | no | nit | The `detent` reference's Environment section (`website/src/content/docs/reference/cli/detent.md:79-82`, three variables) omits `DETENT_READER_MAPPING_BYTES`, which `get` reads (`main.cc:214`) | Code as cited | Name it there |
| F8 | no | defect | Python `Reader(store, uid, namespaces)` keeps UTF-8 pointers borrowed from the caller's list while `detent_open` runs with the GIL released (`bindings/python/detent_python.cc:169-199`); another thread clearing that list frees them under the copy | Code as cited (not reproduced) | Take a `PySequence_Tuple` copy, which owns its items, before releasing the GIL |
| F9 | no | nit | `detent::testing::Store` states no thread rule, and `publish` on a moved-from `Store` hands NULL to the C face, which dereferences it (`src/detent_testing/generation_writer.cc:336-346`) | Probes 261123-151832-28740a and 261123-160814-6f0c98 (`store_probe`: SIGSEGV) | Throw from a moved-from `Store` (or check NULL in the C face), and say one thread publishes at a time |
| F10 | no | nit | With `DETENT_BUILD_TESTS=ON`, the default, `--component development` installs the component though `DETENT_INSTALL_TESTING` is off: the explicit component is the opt-in D149 asks for, and the option only builds the libraries. `detent_testing_shared` builds in every configure with local publication, tests, bindings and the option off included (`src/detent_testing/CMakeLists.txt:11-19`) | Probes 261123-151832-28740a and 261123-160814-6f0c98 (P1, P1b) | Say what the option does in its help text; build the shared twin under `detent_reader_shared`'s condition |
| F11 | no | gap | `tests/docs/mutants.py` M3 renames `defaults_path` on `configuration.md`, which no longer has it (`tests/docs/mutants.py:86-96`); A10's grep excludes the file (contributor Ruling 9) rather than retargeting M3. CI does not run it | Code as cited | Retarget M3 at a key that remains |
| F12 | no | nit | A10's grep excludes more than the criterion lists (`docs/design`, `docs/handoffs`, `docs/dev/docs-site-audit.md`, `DOCUMENTATION_FINDINGS`, `benchmarks/container-alternative/`, `test_agent_cli.cmake`, `tests/docs/mutants.py`) on the contributor's Ruling 9, not a lead ruling | `bindings/tests/legacy_grep.sh`; journal Ruling 9 | The lead confirms or narrows the list |
| F13 | no | gap | reader.md's T5, `get`'s output identical to the old binary's over a 2,000-knob store (`docs/design/reader.md:231`), belongs to S7, which D148 folded into this quest; A18 checks the option set, not identity. My A/B covers 15 reads on one store. A T5 store written as one entry would also miss F1 | Code as cited | Build T5 on a store with one entry per namespace, with F1's fix |
| F14 | no | nit | The one-entry premise is stale in `docs/mechanisms/reader-core.md:23` ("Today's store has one table entry") and `docs/design/client-local-source.md:136` | Code as cited | Correct both with F1 and F2 |
| F15 | no | nit | A2's text says `scenarios.json` has 41 cases; it has 56 (27 `get_as`, 14 `get`, 10 `lookup`, 5 `revision`) | `.campaign/detent/quests/legacy-h.md` A2 | The lead's text |

## Not run

- My probes on Linux: they ran on macOS only; Linux rests on CI 45284336540 at the head and
  45269732772 at Detent:62293129. macos-x86_64 had not finished at the head.
- A `Reader` opened directly in C++, Python or Rust on the multi-entry store: it is the same
  `detent_open` that `detent get` calls (F1), so I did not repeat it per language.
- Sanitizers (CI's), FreeBSD, free-threaded Python, and F8's race (read, not reproduced).

This review ran as a subagent of the lead's session, not as a session titled
`detent · reviewer legacy-h correctness`.
