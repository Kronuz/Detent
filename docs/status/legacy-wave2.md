> Status: CURRENT (2026-11-04) ledger of legacy removal wave 2, steps B to F; H is blocked.

# legacy-wave2

- Branch / worktree: `legacy/wave2` from `Detent:ff0167d`, at `.claude/worktrees/agent-a7a2f6f8bc357662d`.
- State: B, C, D and E are built on the branch; proof and CI are under Evidence.
- Done criteria (set by the lead): one commit per step B, C, D, E; a full configure and build of every remaining target; the full native suite; separate ASan/UBSan and TSan builds of the authority and relay suites; no reference to a deleted symbol, file, target or option outside historical records; CHANGELOG entry; CI green.
- Done: - Rebased by the lead on `Detent:f7267d5` (M1 pins at head); the one conflict, `cmake/dependencies.json`, resolved as main's pins minus `cluster`, `generation_cache` and `http_log`; `tools/check_pins.py`: 30 libraries, 36 shared declarations, 0 mismatches.
- Hosted CI on the rebased head `Detent:8340b53`: run 41812264929, every job green (ASan/UBSan and TSan included); before the rebase, run 41808138967 on `Detent:3f08e98`, every job green.
- Not done before the developer stopped at the spend limit: the leftover-reference grep across docs and website. It's a follow-up, not a landing condition.
- Next: G (the shm agent and unmanaged `detent-agent`; waits on the owner decisions below), H (Client and the bindings; blocked, see Step H), I (flat-v1).
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

No brief for H is in the repository, so this is read from the code. The Client has three source shapes in `InitOptions`: `source_path` (file), `source_poll` (a user function returning full snapshots or merge-patches) and `read_source` (managed generations); `read_source` is already documented as the only non-legacy one (`client.hh`: "Mutually exclusive with legacy file/poll sources"). Removing the legacy two narrows a documented public API (`website/.../reference/cpp-api/sources.md`, the spec's section 10 extension seam), which is the owner's call, and the file source cannot go before step G settles the unmanaged agent.

Who uses them today (grep over `src`, `include`, `tests`, `bindings`, `examples`, `benchmarks`, `tools`):

1. `source_path`: `detent-agent` (unmanaged mode), the Python and Rust bindings (file source only), `test_file_source`, `test_read_view`, `test_agent_cli`, `detent_read_bench`, and two `tests/detent_system` fixtures.
2. `source_poll` (with `SourcePayload`, `is_patch`, the patch merge in `Client::run_generic_source`): no production code since F; `test_client_publication_failure`, `test_read_view`, `test_file_source`, `test_model_and_schema` and `detent_read_bench` use it as an injectable source.

Options for the owner:

- A. Remove `source_poll` only (`SourcePoll`, `SourcePayload`, `is_patch`, `run_generic_source`, the patch path in the Client and `source_id`). Keeps the file source, the unmanaged agent and the bindings. About 5 test and bench files move to a file or `read_source` fake; independent of G.
- B. Remove both legacy sources: the Client becomes defaults, CLI overrides and `read_source`. Needs G to remove the unmanaged agent first, a Python and Rust binding of `make_local_read_source` (new API design; the bindings are file-only today), and rewrites of most `tests/detent` suites. Largest, and it removes the file source the roadmap lists as Done.
- C. Keep both and stop calling them legacy.

Recommendation: decide G first. If the unmanaged agent goes, B is the greenfield shape and H becomes its own design task with a designer for the binding API; if it stays, A, which is mechanical.
