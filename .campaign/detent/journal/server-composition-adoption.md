# server-composition-adoption: journal

State: waiting on jobs 261121-131314-98a5a7 (A2 authority: build all, daemon tests, sigterm), 261121-131321-a5da1a (authority-only build + docs check) and CI at the pushed head (A3)
Quest: .campaign/detent/quests/server-composition-adoption.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

detent-authority is composed through task::lifecycle (commits 74465afb, the listen-line fix, 7eac74e0
pins task at f937bc2). detent_cli/daemon.hh re-exports task's process.hh (same signatures, so the relay
and agent callers keep building). Job 261120-034130-d7a901 builds all three daemons at the head and
runs `ctest -R "daemon|process"`; then run `tests/detent_authority/test_daemon_sigterm.sh` as a job
(A2, authority) and fix what fails. docs/mechanisms/server-composition.md is the A1 note (authority
row done; relay and agent rows wait on the files below).

## Next

1. Read 98a5a7 and a5da1a; fix failures. Read CI at 26a96e4a (ci/macos-arm64 gates A3).
2. Relay and agent adoption and D125 (A4) once the lead widens the file set (Blocked on, 2).

## Blocked on

Lead; recommendation first.

1. **Pin wave owns cmake/dependencies.json; the branch keeps one line of it.** lifecycle.hh and
   process.hh exist only at task >= 3ce0f0c; main pins task bdafa5f1. Reverting the line breaks every
   build of this branch (daemon.hh and main.cc include them), so the branch carries `task` at f937bc2
   (7eac74e0) only so jobs build. Consequence: CI's pin-check fails here (completion, http_lib, reactor
   declare task bdafa5f1: `tools/check_pins.py`, 3 mismatches). Drop the line when pin-wave's commit
   lands (merge main, take its side). Option (b): pin-wave lands first, then I revert it.
2. **File set must widen for the relay and agent halves and D125.** `src/CMakeLists.txt` (already
   edited here: detent_cli links task::asio, daemon.cc dropped; reader-s6 and dense-distribution-build
   also touch this file: a 5-line hunk), `src/detent_relay/host.cc`, `src/detent_relay/configuration.cc`,
   `src/detent_agent/managed_host.cc`, `src/detent_agent/config.cc`, `src/detent_authority/service.cc`
   (D125's merged file), `include/detent/owner_loop.hh`, `tests/*/CMakeLists.txt`.
3. **Other files this branch changes outside the set, kept, each needed:**
   - `docs/mechanisms/server-composition.md`: the A1 deliverable.
   - `tests/detent_authority/test_daemon_sigterm.sh`: the A2 test (not yet registered in tests/CMakeLists.txt, which is outside the set; the job runs it by path).
   - `website/src/content/docs/reference/cli/detent-authority.md` and `.../reference/configuration.md`: the authority's five new flags and keys; tests/docs/check_docs.py (the Docs workflow's check-docs job) fails on any undocumented one.
   - `src/CMakeLists.txt`: see 2.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | docs/mechanisms/server-composition.md (authority row and deviations; relay and agent pending) |
| A2 | authority only: job 261121-131314-98a5a7 (build all three daemons, ctest -R "daemon\|process", test_daemon_sigterm.sh); relay and agent pending the file set |
| A3 | ci:macos-arm64 at the head (pushed 26a96e4a; main merged in de77cc40). Earlier CI failure at a7d0c456 was the branch's: detent_cli lacked task's include path for process.hh (macos-arm64, e2e, all Build steps); fixed in 26a96e4a, checked by job 261121-131321-a5da1a (authority-only build). Docs check-docs: 10 undocumented authority flags/keys, now on the reference pages. pin-check fails until pin-wave (Blocked on, 1) |
| A4 | not started (blocked on files) |

## Result

Not ready for review.

## Rulings I made

- Authority exit codes: a bad option value exits EX_USAGE (note step 1); other errors still exit 1.
- Defaults max_clients 1024, max_in_flight 1024, idle_timeout_ms 75000 (the note's Settled 5): the authority gains a 75 s idle close it did not have.
- The listening line is logged when http starts, not at ready, because the existing daemon tests read the port from it while the parent still waits.
- 26a96e4a: detent_cli links task::asio and drops its empty daemon.cc (cause of the CI Build failures); documented the new flags on the website reference pages.
- Kept the 5 ms leader wait as a `sleep_for` inside `LeaderComponent::start`, recorded as a deviation ending with loop-idle-waits.
