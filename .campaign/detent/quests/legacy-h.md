# legacy-h: Legacy H, the Client goes and the bindings move to the reader core

Risk: critical. The lead writes this quest; contributors read it and never edit it. Its machine
state (status, branch, checks, reviews) is in `state.json`: `campaign status legacy-h`.

## Outcome

`detent::Client`, `InitOptions`, `health()`, `Metrics`, the read views, `LocalReadSource` and both
factories, `ClientSourceOptions`, `detent::global`, `TestKnobs`, `OverrideTable`, `SnapshotRef`, the
first evaluator and the tests that covered only them are gone. `Reader` over `libdetent_reader` is
the one read API in C, C++, Python (a C extension) and Rust (a crate over the hand-declared C ABI,
no C++ in cargo). Shared twins export only `detent_*` and `detent_testing_*`; a `bindings` CI job
runs the scenario, export and binding tests. `detent get` reads through `GenerationReader` with no
`--defaults` until reader S7. Docs, website and diagrams describe only what remains.

## Acceptance criteria

Numbering cites `legacy/wave2-h:docs/status/legacy-wave2.md`: the wave's done criteria ("wave n",
set by the lead) and Step H's B1-B6.

- A1 B1: `detent_reader_shared` and `detent_testing_shared` export only `detent_*` and `detent_testing_*`; `test_reader_exports` and `test_testing_exports` read the symbol table: the `native` gate
- A2 B2: `bindings/tests/scenarios.json` (41 cases) passes through `test_reader_scenarios`: the `native` gate
- A3 B3: the Python C extension passes `bindings/python/tests/test_detent.py` T1-T4 (scenarios, store states, eight threads against 100 publishes, descriptors, threads and fork): the `native` gate
- A4 B4: the Rust crate passes `tests/integration.rs` (T1-T3) and `tests/lifetime.rs` (T4): the `native` gate
- A5 B5: the scenario, export and binding tests pass `ctest --repeat until-fail:3`, the `bindings` job's command: a job; the hosted `bindings` jobs (ubuntu-24.04, macos-15 with `-fexperimental-library`) are in the CI gate
- A6 B6: the deleted types and tests are gone and their ports (`test_lazy_page_access`, the oracles, `prepared_client_probe`, `test_evaluation`, `w5_detent`, `detent_demo`, `tests/cmake_consumer`) build and pass: the `native` gate
- A7 A full configure and build of every remaining target, Python and Rust on (wave 2): the `native` gate
- A8 The full native suite passes (wave 3): the `native` gate
- A9 Separate ASan/UBSan and TSan builds of the authority and relay suites pass (wave 4): the `asan` and `tsan` gates
- A10 No reference to a deleted symbol, file, target or option outside historical records, including `docs/SCALING.md:54` and `website/.../basics.md:78` (wave 5): a grep job over `src include tests bindings benchmarks examples tools docs website README.md`, excluding `docs/reviews`, `docs/measurements`, `docs/perf`, `docs/status` and `CHANGELOG.md`
- A11 [artifact] CHANGELOG entry (wave 6): `CHANGELOG.md`
- A12 T5 mutants die: a binding that ignores the fallback, drops `hash`, or maps UNTRUSTED to OK, in Python and Rust (six; H evidence): mutant jobs at the landed code, after touching the mutated sources
- A13 `test_system_*` pass with `.ccache/` inside the checkout (the 64 KiB `evidence.py` failure; handoff step 2.2, lead ledger criterion 2): a job
- A14 [artifact] The website's reader reference replaces the Client pages (`client.md`, `sources.md`, `global-and-testing.md`, `guides/live-updates.md`, diagram `06_adapters` deleted): `website/src/content/docs/reference/cpp-api/reader.md`
- A15 [decision] Removing both legacy Client sources (wave decision 8): D13
- A16 [decision] Deleting `detent::Client` and `health()`, `Reader` the one read API: D14
- A17 [decision] `detent get` keeps `GenerationReader` and loses `--defaults` until S7; position-independent code build-wide (H "Design vs code" 1; handoff owner question 5): D21
- A18 `detent get` reads through `detent::Reader` (D148): it accepts `--reader-mapping-bytes` and refuses the other `--reader-*` flags, `--reader-validation full` and `--defaults`; a test checks both by value, and no `GenerationReader` path remains in `get`: the `native` gate
- A19 `libdetent_testing` installs as an opt-in development component (D149): `tests/cmake_consumer` (or a sibling consumer) builds an application test against the installed library that publishes knob values into a test store and reads them through `Reader`, and the Python and Rust tests do the same through the C face: the `native` gate
- A20 The design review's F1-F4 and F7 (`.campaign/detent/reviews/legacy-h-design.md`) are fixed, each with a test or job that fails without it; `docs/design/client-local-source.md` describes the bindings over the current ABI
- A21 The reader core reads namespace delivery's table, one entry per namespace (docs/design/reader.md "Store", A1-A2; docs/mechanisms/reader-core.md's "Today's store has one table entry" goes): `find` selects a namespace's entry by name, a handle opened with a namespace list follows those, one opened with none follows all, and per-namespace sequences decide refresh (reader.md D-rules). `detent get`, Python and Rust read a multi-namespace agent store by value; the correctness review's F1/F2 probe (job 261123-151840-af4466, the website's pages on an agent store) reads what main's `get` reads; a `--fails-without` job at the one-entry core fails it: the `native` gate and jobs
- A22 The Docs gate is green: `check-docs` and the website's e2e blocks pass (review F3): `ci:ci`

CI (wave 7, "CI green"; handoff step 2) is the landing's CI gate (REPORT O2), on the merged head
after `reader-s6` lands; the macos-15 bindings fix is unproven until it runs.

## Contracts

Consumes `reader-s6`'s C ABI and `libdetent_reader`. Provides the Python module `detent`
(`Reader`, `Status`, `ReaderError`, `Lookup`), the Rust crate `detent`, the shared libraries'
export lists, and `detent get`'s option set without `--defaults`. Removes `detent::Client` and every
type listed under Outcome.

## Decisions

1. Owner rulings D13 and D14; the design is `docs/design/client-local-source.md`, approved by the
   owner on 2026-11-04 (history quest `client-local-source`).
2. Lead default D21 stands unless the owner rules otherwise; the ledger marks it an owner call.
3. Open (lead): critical risk needs a `design` acceptance. The design had owner approval and no
   independent design review on record; decide whether a design reviewer reviews it now.
4. H's leftovers (free-threaded Python, fork in the Rust tests, the unused `atomic-shared-ptr`
   link) are follow-ups in PLAN.md, not criteria (handoff, Follow-ups 5).
5. Before trusting a local binding test, `touch bindings/python/detent_python.cc bindings/rust/src/lib.rs`
   (stale mutant binaries).

## May land early

None. It deletes public API.
- From docs-site-audit: remove `detent-agent --upgrade-managed` (`src/detent_agent/main.cc:116`), an upgrade path D43 rules out.
