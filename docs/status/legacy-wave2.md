> Status: CURRENT (2026-11-04) ledger of legacy removal wave 2, steps B to G; H waits on the owner (see Step H).

# legacy-wave2

- Branch / worktree: `legacy/wave2` from `Detent:ff0167d`, at `.claude/worktrees/agent-a7a2f6f8bc357662d`.
- State: B, C, D and E are built on the branch; proof and CI are under Evidence.
- Done criteria (set by the lead): one commit per step B, C, D, E; a full configure and build of every remaining target; the full native suite; separate ASan/UBSan and TSan builds of the authority and relay suites; no reference to a deleted symbol, file, target or option outside historical records; CHANGELOG entry; CI green.
- Done: - Rebased by the lead on `Detent:f7267d5` (M1 pins at head); the one conflict, `cmake/dependencies.json`, resolved as main's pins minus `cluster`, `generation_cache` and `http_log`; `tools/check_pins.py`: 30 libraries, 36 shared declarations, 0 mismatches.
- Hosted CI on the rebased head `Detent:8340b53`: run 41812264929, every job green (ASan/UBSan and TSan included); before the rebase, run 41808138967 on `Detent:3f08e98`, every job green.
- Not done before the developer stopped at the spend limit: the leftover-reference grep across docs and website. It's a follow-up, not a landing condition.
- Next: H (Client and the bindings; waits on the owner's answer to the binding design's Q1 and on reader S6, see Step H), I (flat-v1). F and G are built; see their sections.
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

## Step H: the Client and the bindings (blocked, nothing changed)

Update after the owner ruling (decision 8): the lead stopped H after G. The binding design (`design/client-local-source`, `Detent:38afcb5`) makes both bindings wrap `libdetent_reader`'s C ABI with a `Reader` type (reader.md S8 and S9), and asks the owner whether H deletes `detent::Client` altogether (its Q1). What H removes depends on that answer, and the bindings (B1 to B5) wait on reader S6. No H code is on any branch.

No brief for H is in the repository, so this is read from the code. The Client has three source shapes in `InitOptions`: `source_path` (file), `source_poll` (a user function returning full snapshots or merge-patches) and `read_source` (managed generations); `read_source` is already documented as the only non-legacy one (`client.hh`: "Mutually exclusive with legacy file/poll sources"). Removing the legacy two narrows a documented public API (`website/.../reference/cpp-api/sources.md`, the spec's section 10 extension seam), which is the owner's call, and the file source cannot go before step G settles the unmanaged agent.

Who uses them today (grep over `src`, `include`, `tests`, `bindings`, `examples`, `benchmarks`, `tools`):

1. `source_path`: `detent-agent` (unmanaged mode), the Python and Rust bindings (file source only), `test_file_source`, `test_read_view`, `test_agent_cli`, `detent_read_bench`, and two `tests/detent_system` fixtures.
2. `source_poll` (with `SourcePayload`, `is_patch`, the patch merge in `Client::run_generic_source`): no production code since F; `test_client_publication_failure`, `test_read_view`, `test_file_source`, `test_model_and_schema` and `detent_read_bench` use it as an injectable source.

Options for the owner:

- A. Remove `source_poll` only (`SourcePoll`, `SourcePayload`, `is_patch`, `run_generic_source`, the patch path in the Client and `source_id`). Keeps the file source, the unmanaged agent and the bindings. About 5 test and bench files move to a file or `read_source` fake; independent of G.
- B. Remove both legacy sources: the Client becomes defaults, CLI overrides and `read_source`. Needs G to remove the unmanaged agent first, a Python and Rust binding of `make_local_read_source` (new API design; the bindings are file-only today), and rewrites of most `tests/detent` suites. Largest, and it removes the file source the roadmap lists as Done.
- C. Keep both and stop calling them legacy.

Recommendation: decide G first. If the unmanaged agent goes, B is the greenfield shape and H becomes its own design task with a designer for the binding API; if it stays, A, which is mechanical.

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
