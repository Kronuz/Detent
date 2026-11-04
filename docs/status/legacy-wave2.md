> Status: CURRENT (2026-11-04) ledger of legacy removal wave 2, steps B to E.

# legacy-wave2

- Branch / worktree: `legacy/wave2` from `Detent:ff0167d`, at `.claude/worktrees/agent-a7a2f6f8bc357662d`.
- State: B, C, D and E are built on the branch; proof and CI are under Evidence.
- Done criteria (set by the lead): one commit per step B, C, D, E; a full configure and build of every remaining target; the full native suite; separate ASan/UBSan and TSan builds of the authority and relay suites; no reference to a deleted symbol, file, target or option outside historical records; CHANGELOG entry; CI green.
- Done: - Rebased by the lead on `Detent:f7267d5` (M1 pins at head); the one conflict, `cmake/dependencies.json`, resolved as main's pins minus `cluster`, `generation_cache` and `http_log`; `tools/check_pins.py`: 30 libraries, 36 shared declarations, 0 mismatches.
- Hosted CI on the rebased head `Detent:8340b53`: run 41812264929, every job green (ASan/UBSan and TSan included); before the rebase, run 41808138967 on `Detent:3f08e98`, every job green.
- Not done before the developer stopped at the spend limit: the leftover-reference grep across docs and website. It's a follow-up, not a landing condition.
- Next: F (the `detent_http` library), G (the shm agent and unmanaged `detent-agent`), H (Client and the bindings), I (flat-v1).
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
