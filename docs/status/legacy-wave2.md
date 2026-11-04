> Status: CURRENT (2026-11-04) ledger of legacy removal wave 2, steps B to H.

# legacy-wave2

- Branch / worktree: `legacy/wave2` from `Detent:ff0167d`, at `.claude/worktrees/agent-a7a2f6f8bc357662d`.
- State: B, C, D and E are built on the branch; proof and CI are under Evidence.
- Done criteria (set by the lead): one commit per step B, C, D, E; a full configure and build of every remaining target; the full native suite; separate ASan/UBSan and TSan builds of the authority and relay suites; no reference to a deleted symbol, file, target or option outside historical records; CHANGELOG entry; CI green.
- Done: - Rebased by the lead on `Detent:f7267d5` (M1 pins at head); the one conflict, `cmake/dependencies.json`, resolved as main's pins minus `cluster`, `generation_cache` and `http_log`; `tools/check_pins.py`: 30 libraries, 36 shared declarations, 0 mismatches.
- Hosted CI on the rebased head `Detent:8340b53`: run 41812264929, every job green (ASan/UBSan and TSan included); before the rebase, run 41808138967 on `Detent:3f08e98`, every job green.
- Not done before the developer stopped at the spend limit: the leftover-reference grep across docs and website. It's a follow-up, not a landing condition.
- Next: I (flat-v1). F and G have landed; H is built on `legacy/wave2-h`; see their sections.
- Blocked on: nothing blocks this wave. Product decisions for the owner, found by the website sweep:
  1. With no configuration the agent polls `127.0.0.1:8317` (`src/detent_agent/main.cc:128`), which no Detent daemon serves. Step G removes or changes it.
  2. Roadmap item 4 reads "a path for unmanaged agents and clients to receive authority data". Keep or drop it?
  3. The words "legacy" and "classic" remain for the agent's "legacy mode" (matches its help text), the client HTTP poll source and `architecture/authority.md`'s comparison with `cluster::Raft`; steps F, G and H own them.

## Decisions

1. `DETENT_BUILD_DISTRIBUTOR` is removed. `DETENT_BUILD_AUTHORITY` (now `ON` by default) builds `detentd`, the authority libraries that need consensus, and their tests and benchmarks. `DETENT_BUILD_RELAY` and `DETENT_BUILD_MANAGED_AGENT` are unchanged.
2. `--authority CONFIG` stays a required flag of `detentd` (or the `authority` field of `detentd.json`), because the packaging, fixtures and docs pass it; renaming it is a separate change.
3. `kLoopbackHost` lives in `src/detent_cli/config.hh` next to `kDefaultPort`.
4. `--status` without `--authority` probes `127.0.0.1` over plain HTTP; with `--authority` it probes `publisher_bind`, over HTTPS when the publisher uses TLS. See `docs/mechanisms/status-probe.md`.
5. `test_authority_cluster` used `admin::Runtime` for its offline recover-and-checkpoint check; the class is now `OfflineRuntime` in `tests/detent_authority/offline_runtime.hh`, trimmed to what the test calls.
6. Owner ruling: "Starting with no source configured is a hard error: "no source configured", nonzero exit. The owner accepted either that or an empty snapshot with a warning; the lead chose the hard error." Tested in `test_agent_cli`.
7. Owner ruling: "Drop the unmanaged agent and remove the legacy pieces with it. Step G removes the shm agent and the unmanaged `detent-agent` path. Roadmap item 4 is dropped."
8. Owner ruling: "Step H takes option B: remove both legacy Client sources, `source_poll` and `source_path`." Order: G, then H (C++ side on `legacy/wave2-h`, stacked on G); the Python and Rust binding rewrite and the Client tests that depend on it wait for the `design/client-local-source` design.

## Design vs code

No design document covers this wave; the brief is the design. Differences from the brief:

1. The brief names `cluster` and `generation_cache` for removal from `cmake/dependencies.json`; `http_log` is removed too, because only the classic access log used it.
2. `test_http_connection_aging` and `test_multi_node_poll_source` (in `tests/detentd/`) drove `detent_http` against the classic distributor and are deleted with it; F should add an authority-backed replacement if it wants that coverage.

## Evidence

- Rebased by the lead on `Detent:f7267d5` (M1 pins at head); the one conflict, `cmake/dependencies.json`, resolved as main's pins minus `cluster`, `generation_cache` and `http_log`; `tools/check_pins.py`: 30 libraries, 36 shared declarations, 0 mismatches.
- Hosted CI on the rebased head `Detent:8340b53`: run 41812264929, every job green (ASan/UBSan and TSan included); before the rebase, run 41808138967 on `Detent:3f08e98`, every job green.
- Not done before the developer stopped at the spend limit: the leftover-reference grep across docs and website. It's a follow-up, not a landing condition.

## Step F: the `detent_http` library

- Branch / worktree: `legacy/wave2-f` from `Detent:3e8bdbc`, at `.claude/worktrees/agent-a6c13d9faa82afd4d`.
- Removed: `src/detent_http`, `include/detent_http`, `tests/detent_http` (`test_http_socket_ownership`, `test_http_response_framing`), the `detent_http` target and its `tests/CMakeLists.txt` entry; `detent-agent` no longer links it.
- The agent's only HTTP consumer went with it, so `--source-address`, `--source-http`, `DETENT_SOURCE_ADDRESS`, `DETENT_SOURCE_HTTP` and the `source_address` and `source_http` config fields are gone from `detent-agent` and `detent get` (usage text, `agent.json` schema, argument validation). Each is now an unknown argument or field.
- The 127.0.0.1:8317 default poll (owner question 1 of this wave) goes with it, because the agent can no longer poll: with no source and no defaults the agent starts with an empty snapshot, as its header comment already says. Step G keeps nothing to decide there; the owner should confirm that reading.
- `InitOptions::source_poll` and `SourcePayload` stay in the Client; no in-tree code sets them (tests do). Step H.
- Coverage: the two `detent_http` tests exercised only the deleted client. `test_http_connection_aging` and `test_multi_node_poll_source` (deleted in E) needed a classic distributor and the client both; an authority-backed replacement has nothing to run against, because no Detent daemon serves `GET /v1/snapshot`, so none is added. The TLS delivery client keeps its own suites (`test_delivery_*`).
- New tests (`tests/detent_agent/test_agent_cli.cmake`): the config field `source_address`, `detent-agent --source-address` and `detent get --source-address` are rejected; the duplicate-field check now uses `source_path`.
- Docs, website, diagrams (`05_update_path`, `08_detent_deps` re-rendered with d2), README, CHANGELOG: every `detent_http`, `make_http_poll_source` and `source_address` reference is removed or rewritten (grep is clean outside the CHANGELOG, this ledger and measurement logs).

Evidence (F):

- Configure and build, Release, every target: clean. Full ctest: 218 of 219 pass on first run; the failure was my new test leaving a bad `agent.json` in place for the next check. Fixed, then `test_agent_cli` and `test_system_agent` pass on the rebuilt binaries. `test_local_trusted_paths` is skipped (not applicable on this host), as before.
- Mutant: adding `--source-address` back to the agent's accepted flags makes `test_agent_cli` fail (the agent starts instead of rejecting the flag; timeout at `test_agent_cli.cmake:34`). Reverted.
- Sanitizers: none run locally (no threaded or memory code added); the hosted ASan/UBSan and TSan jobs cover them.
- Hosted CI, dispatched on `Detent:3cf9201` (run 41867110024; the branch name does not trigger CI on push): pin-check, source-checks, linux-read-counters, linux-benchmarks, linux-x86_64-gcc and clang, linux-arm64-gcc, ASan/UBSan and macos-x86_64 green. linux-tsan failed once on `test_local_paged_publication_artifacts` ("invalid paged artifact operation accepted", 0.26 s; code F does not touch) and passed on rerun. macos-arm64 was cancelled by the first attempt and its rerun was still queued after more than an hour when the developer stopped: the one job not yet seen green.

## Step H: the Client and the bindings

- Branch / worktree: `legacy/wave2-h`, stacked on `reader/s6` (merged with main and with S6 as they moved), at `.claude/worktrees/agent-a6c13d9faa82afd4d`. Rulings: decision 8, and the owner's "Delete Detent::Client and health() if they're legacy" (Q1 (a), Q2 (a) of `docs/design/client-local-source.md`).
- B1: `libdetent_reader` and `libdetent_testing` have shared twins (`detent_reader_shared`, `detent_testing_shared`) that export only `detent_*` and `detent_testing_*` (a version script, or an exported-symbols list on Apple); `test_reader_exports` and `test_testing_exports` read the symbol table. The static targets stay for the C++ tests and the `detent` reader. Static dependencies build position-independent (`CMAKE_POSITION_INDEPENDENT_CODE`, set once after `project()`).
- B2: `bindings/tests/scenarios.json` (41 cases: values including INT64_MIN and MAX, the fallback on each error, hash and switch pairs, rollout 0 and 100, group precedence, a namespace outside the handle, revisions across two publishes) and its C++ runner `test_reader_scenarios`.
- B3: the Python module is a C extension (`bindings/python/detent_python.cc`, `METH_FASTCALL`, `Py_MOD_GIL_NOT_USED`, no subclassing, no `close()`); `Status`, `ReaderError` and `Lookup` are small Python types the module builds at import. `bindings/python/tests/test_detent.py`: T1 scenarios, T2 store states, T3 eight threads against 100 publishes, T4 descriptors, threads and fork, and the Python-only argument checks.
- B4: the Rust crate (`bindings/rust`) declares the C ABI by hand; `build.rs` is a link search, `dylib=detent_reader` and an rpath, and cargo compiles no C++. `tests/integration.rs` (T1 to T3) and `tests/lifetime.rs` (T4, a process of its own so descriptor counts are exact). `serde_json` is the only dev-dependency.
- B5: the `bindings` job in `ci.yml` (ubuntu-24.04 and macos-14): builds `detent_python detent_rust detent_testing_shared test_reader_scenarios` and runs `ctest --repeat until-fail:3` over the scenario, export and binding tests.
- B6: deleted `detent::Client`, `InitOptions`, `health()`, `Metrics`, the read views, `LocalReadSource` and both factories, `ClientSourceOptions`, `detent::global`, `TestKnobs`, `OverrideTable` (nothing outside the Client needed it: the three other users only included it), `SnapshotRef`, the first evaluator, `detail/file.hh`, and the nine tests that covered only them. `KnobError::what()` moves to `src/detent/errors.cc`. `tests/detent/test_duplicate_precedence` and `test_model_alloc_failure` drop their override-table cases; `actual_type_sweep` declares its own variant.
- Ports: `test_lazy_page_access` (a `detent::Reader` check), `local_oracle` and `mixed_oracle` (`GenerationReader` and `ReadView`; the mixed oracle's benchmark now compares `Reader` with a pinned view, path label `reader`), `prepared_client_probe` (`detent::Reader`), `test_evaluation` (the removed evaluator is now a reference written in the test), `w5_detent` (rollout buckets from `stable_bucket`; the `--just-knob` parser rows were `w5_stox`'s corpus already), `detent_demo` (opens a `Reader`), `tests/cmake_consumer`.
- Benchmarks: `detent_read_bench` and `detent_model_lookup` are deleted; `reader_bench` (S6) replaces the first. `detent_update_bench` and `merge_over_defaults` are deleted too: the merge over baked defaults was the Client's. `bench_rows.sh` loses its `read` kind.
- Docs: the website (`reference/cpp-api/reader.md` added; `client.md`, `sources.md`, `global-and-testing.md`, `guides/live-updates.md` and diagram `06_adapters` deleted), README, USAGE, ACTORS, SCALING (the stale `/v1/snapshot` line) and `basics.md` (same), CHANGELOG. A docs agent did the sweep; I checked its grep.

Design vs code (H):

1. `detent get` is not on the reader core (reader.md D8, S7). It reads the durable generation through `GenerationReader` and `ReadView`, because the core has no open options for generation identity binding (`--generation-cluster` and the three other flags) or for the reader capacity limits, and `test_managed_process` exercises both. What changes versus before: no `--defaults` (R1), so a knob the generation does not hold is an error or `--default VALUE`. Moving `get` to the core is S7's remaining work and needs those two options in the ABI first; **owner call**, because dropping identity binding from `get` would narrow a check.
2. The design lists `detent_demo`, `model_lookup` and the probes; it does not list `detent_update_bench` and `merge_over_defaults`. Both go with the baked-defaults layer.
3. `test_eval_semantics` is deleted; its rollout-boundary and group-precedence cases are rows of `scenarios.json`, and `test_evaluation` keeps the exhaustive small-domain comparison.
4. The core's lookups cannot report WRONG_TYPE through `Reader.lookup` (it reads the knob's own kind), so wrong-type rows check the returned fallback only.
5. `test_managed_process.sh` passes `--default 7` to `detent get` where a deleted knob used to fall back to a baked 7; nothing else in the script changed meaning.
6. `atomic-shared-ptr` is still linked by `detent` (`src/CMakeLists.txt`) though no source uses it now; left for the pin owner.

Evidence (H):

- Release configure and build of every target with `DETENT_BUILD_PYTHON=ON DETENT_BUILD_RUST=ON`: clean (one link error found and fixed on the way: `KnobError::what()` lived in `client.cc`). Full ctest, 216 tests: the bindings (`detent_python_test`, `detent_rust_test`), the scenario and export tests, `test_agent_cli`, `test_reader_capacity`, the managed-process and system suites pass. First full run at `-j4` also showed `test_system_*` failing because `evidence.py` refuses a `git diff HEAD` over 4 MiB with the deletions uncommitted (it passes, 10 of 10, once committed) and `test_reader_switch` failing once with `refresh status 5: invalid local generation descriptor` (`test_reader_switch.cc:237`) in the 16-reader publish storm under that load; alone it passes in 260 s. That is S6's test over S6's code; the lead should watch it.
- Mutants (T5, `.scratch/mutants.py`): a binding that ignores the fallback (Python `eval` and Rust `get` pass a fixed fallback to the core), one that drops `hash` (Python `eval`, Rust `lookup`), one that maps UNTRUSTED to OK (both): all six KILLED. A first "start the output from false" mutant survived in both languages because the core writes the fallback itself; it was equivalent and replaced.
- Sanitizers: hosted ASan/UBSan and TSan jobs; nothing threaded or memory-owning was added beyond the extension's one handle.

State and Next (H), written when the developer stopped on a credit limit:

- Branch `legacy/wave2-h` is pushed, head `Detent:7e3960e` at the time of writing (merged with `reader/s6` `Detent:8768732` and main `Detent:32aa0c8`; merge again as either moves). B1 to B6 are all built and committed: `Detent:07aa10a` (B1, B2), `Detent:d210031` (B3 to B5), `Detent:1c87840` (B6 and the docs), then the ledger, a macOS and lazy-page fix `Detent:90a51b4`, the evidence message and `.ccache/` ignore.
- Landing order: reader S6 must land on main first (this branch contains it; its own CI shows `test_reader_switch` failing on linux-x86_64-clang, run 41921276663, and once locally under load: `refresh status 5: invalid local generation descriptor`, `test_reader_switch.cc:237`; it passes alone in 260 s). Then merge main into this branch, re-run CI, land H.
- CI, run 41926685309 on `Detent:7e3960e` (dispatch with `gh workflow run ci.yml --ref legacy/wave2-h`; a push to this branch name does not start CI): when written, pin-check, source-checks, benchmarks, read-counters and `bindings-ubuntu-24.04` were green, the rest still running. Check `gh run view 41926685309 --json jobs`. Earlier run 41924362293 found and this branch fixed: `test_local_lazy_page_access` aborting with `detent_open: source_failure` (its fixture has no `change` file, which the core refuses; the test no longer opens the core), `bindings-macos-14` failing configure for lack of `std::jthread` (now macos-15 with `-fexperimental-library`; unproven until the run finishes), and `test_system_authority_writers` and six other `test_system_*` failing on two Linux jobs with `metadata command output exceeds 64 KiB` (`.ccache/` sits inside the checkout and was untracked; now ignored, and `tests/detent_system/evidence.py` prints the start of an oversized output).
- Left to do, in order:
  1. Watch run 41926685309 to green on every job. If `bindings-macos-15` still fails at configure, install Homebrew llvm as the shared workflow does (`macos_llvm`) or pass the libc++ flags it passes.
  2. If a `test_system_*` job still reports the 64 KiB error, the new message names the untracked paths; ignore or relocate them.
  3. Local state: the last full local ctest was 216 tests, with the only non-environmental failure `test_reader_switch` (above). After the fixes above only `test_local_lazy_page_access` needed re-running; `.scratch/rerun-final.sh` does that, plus the scenario, agent-CLI and binding tests, from `.scratch/build` (configured with `-DDETENT_BUILD_PYTHON=ON -DDETENT_BUILD_RUST=ON`). A stale mutant binary can survive in `.scratch/build` and `bindings/rust/target` (mutants restore the file with an old mtime): `touch bindings/python/detent_python.cc bindings/rust/src/lib.rs` before trusting a local binding test.
  4. Owner call (Design vs code 1): `detent get` still reads through `GenerationReader`, not the reader core (S7), because the core has no generation-identity or capacity open options. Moving it needs those in the C ABI first.
  5. Not done: Python on a free-threaded build (T3), fork in the Rust tests, a perf row for `reader_bench` (the lead measures), and removing the unused `atomic-shared-ptr` link from `src/CMakeLists.txt`.
- Commands: build and test everything with the bindings: `cmake -S . -B .scratch/build -DCMAKE_BUILD_TYPE=Release -DDETENT_BUILD_PYTHON=ON -DDETENT_BUILD_RUST=ON && cmake --build .scratch/build -j4 -- -k && (cd .scratch/build && ctest -j4 --output-on-failure)`, each under `quiet light`; binding tests alone: `ctest -R 'detent_python_test|detent_rust_test|test_reader_scenarios'`. Mutants: `python3 .scratch/mutants.py` (six, all killed; scripts are untracked in `.scratch/`).

## Step G: the shared-memory agent and the unmanaged `detent-agent`

- Branch / worktree: `legacy/wave2-g`, stacked on `legacy/wave2-f` (merged with main `Detent:5475baf`), at `.claude/worktrees/agent-a6c13d9faa82afd4d`.
- Removed: `src/detent_agent/{agent,shm_writer,shm_reader}.{cc,hh}`, `shm_format.hh`, the `detent_agent_lib` target, `snapshot.shm`, the tests `test_agent_basic`, `test_snapshot_canonical`, `test_shm_concurrency_stress` and `test_system_agent`, and `benchmarks/detent_agent_shm_bench.cc`. `detent_scale_bench` keeps its JSON measurements and drops the shared-memory image fields.
- `detent-agent` is the managed profile only: `run_agent` throws `no source configured; pass --managed PRIVATE_CONFIG or set managed_path` (exit 1) before it forks or creates a runtime directory. `detent get` throws `no source configured; ...` when neither a generation directory nor `--managed` is set, whatever `--default` or `--defaults` say (decision 6). `detent-agent --status` prints `running`/`not running` and, when a generation directory is set, the durable revision and count.
- `--source-path`, `--poll-interval-ms`, `DETENT_SOURCE_PATH`, `DETENT_POLL_INTERVAL_MS` and the `source_path` and `poll_interval_ms` config fields are removed. `--defaults` is a `detent get` option only; `launch_agent` no longer forwards it to the agent. `DETENT_MANAGED_CONFIG` and `DETENT_GENERATION_DIR` are listed in the usage text.
- Step E leftovers: the systemd `detentd` unit description, the Homebrew caveats and formula test (now checks the `no source configured` error for `detent get` and `detent-agent`), diagrams `01_overview` (was the classic publisher, distributor, client flow), `07_agent` and `08_detent_deps` (re-rendered), `docs/SERVICES.md`, `docs/USAGE.md`, `docs/ACTORS.md`, README and the website reference, architecture and guide pages that described the shared-memory agent or `source_path`. Roadmap item 4 is dropped (decision 7).
- New tests (`tests/detent_agent/test_agent_cli.cmake`): `no source configured` for `detent get` (plain, with `--default`, with `--defaults`, with `--system`) and for `detent-agent` (plain, `--daemonize`, `--system`, `--initialize-managed`), with no runtime directory created; `--status` with nothing configured; the three removed flags and three removed config fields are rejected by both binaries; a configured but empty generation directory answers from `--defaults` or `--default`.
- Historical records left as written because they describe what was measured or reviewed when the agent existed: `docs/AUDIT.md`, `docs/VALIDATED_ARCHITECTURE.md`, `docs/SHARED_MEMORY_PADDING.md`, `docs/design/reader.md`, `docs/DELIVERY_INTEGRATION_ARCHITECTURE.md`, `docs/READER_CAPACITY_ARCHITECTURE.md`, `docs/CONTAINER_SELECTION_ARCHITECTURE.md`, `docs/reviews`, `docs/measurements`, `docs/perf` and the JSON records under `benchmarks/`.

Evidence (G):

- Release configure and build of every target: clean. Full ctest: 215 of 216 pass; `test_local_trusted_paths` is skipped on this host (as before). The one failure on the first run, `test_reader_capacity`, wrote `poll_interval_ms` into a config as a neutral field; it now uses `verbosity`, and passes.
- Mutant: dropping the `no source configured` check from `run_agent` makes `test_agent_cli` fail at its `detent-agent` with no arguments check (the agent goes on to read a missing managed credential). Reverted.
- Sanitizers: none run locally (the change removes code and adds no threaded or memory code); the hosted ASan/UBSan and TSan jobs cover them.

Blocked on (G):

1. The hands-on tutorial is written around publishing a snapshot file and reading it with `detent get --source-path`: `website/.../quickstart.md`, `settings-and-flags.md`, `rollouts.md`, `group-overrides.md`, `next.md`, the `index.mdx` hero snippet and `QUICKSTART.md` (which still drives the removed classic `PUT /v1/snapshot` distributor from step E). Neither the file source nor a one-line replacement exists any more, and the only live path is an authority with credentials plus a TLS-verified managed agent. What the tutorial should run on is a product decision (for example a documented loopback authority and agent recipe, or a direct `detent get --snapshot FILE` mode); I did not invent one and the pages are unchanged. The Client's own `source_path` and `source_poll` docs (README, `cpp-api`, `live-updates`, `cpp-application`, `sources`) wait for step H.

CI:

- Step F, merged head `Detent:3a7a822` (main `Detent:5475baf` merged in): run 41886875818, every job green (macos-arm64 after waiting on runner capacity).
- Step G, head `Detent:4f49f4b`: run 41891107195, every job green (pin-check, source-checks, read-counters, benchmarks, gcc, clang, arm64-gcc, ASan/UBSan, TSan, macos-x86_64, macos-arm64). Main has since moved to `Detent:dba0ef8`; G is not rebased on it.
- Step G after F landed (main `Detent:5092933`): merged main into `legacy/wave2-g` (`Detent:7ad45ff`; the merge brought docs only, no source change) and updated the unmanaged-agent lines of `docs/HOW_IT_WORKS.md`, `website/.../architecture/how-it-works.md` and `docs/status/how-it-works.md` to say it has been removed.
- Step G on merged main, head `Detent:8ec47f3`: run 41898318055, every job green (macos-arm64 and macos-x86_64 included).
