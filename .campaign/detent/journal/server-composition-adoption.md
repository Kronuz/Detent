# server-composition-adoption: journal

State: waiting on jobs 261120-034130-d7a901 (and blocked on the lead for the pin wave and the file set, below)
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

1. Read job d7a901; fix failures in main.cc or the sigterm test.
2. Submit `bash tests/detent_authority/test_daemon_sigterm.sh .scratch/build/src/detent-authority` (--for A2).
3. Push the branch; run the suite (`campaign verify server-composition-adoption`) once the pin question is ruled.
4. Relay and agent adoption and D125, once the lead grants the files.

## Blocked on

Lead, four things; recommendation first.

1. **Pin wave (the quest cannot reach CI green without it).** lifecycle.hh and process.hh exist only at
   task >= 3ce0f0c; Detent pins task bdafa5f1, and http (c813531c, head aed0314), reactor (ab69cf26,
   head 6cd8f73), completion (560c288b, head d8b538c) declare task bdafa5f1 / 18eb516, journal and
   consensus 95798a0. `tools/check_pins.py` with task at f937bc2 reports 3 mismatches (completion,
   http_lib, reactor). A probe commit (task f937bc2 only, old http and reactor) built all three
   daemons: job 261120-031230-bdfb17 exit 0. Options: (a) RECOMMENDED: the libraries repin task to
   f937bc2 (or a later task head) and Detent repins http/reactor/completion/journal/consensus in one
   `campaign pin` commit, which also brings http `Limits`, `raise_open_files` and
   `tls::server_context` (the note's C1, C5 and C2) so main stops using setters; (b) relax
   check_pins for task. My branch carries the single-line task pin (7eac74e0) so jobs build; drop it
   when the lead's pin commit lands.
2. **File set.** Needed beyond main.cc x3 and detent_cli: `src/CMakeLists.txt` (drop
   detent_cli/daemon.cc, now an empty TU); `src/detent_relay/host.cc`, `src/detent_relay/configuration.cc`
   (relay loop, signals, stop, `connections`, `idle_ms`, `ca`; host.cc and the `turn` poll are also
   loop-idle-waits'); `src/detent_agent/managed_host.cc`, `src/detent_agent/config.cc` (monitor, stop,
   agent.json); `src/detent_authority/service.cc` (`load_service_options`, D125's merged file; the leader
   wake; loop-idle-waits' file); `include/detent/owner_loop.hh` (`run()`, Settled 4);
   `tests/*/CMakeLists.txt` (register the sigterm test; A2 and A4 tests for relay and agent).
3. **Order.** The brief says land loop-idle-waits and xapiand:xapiand-lifecycle first; neither has landed.
   relay and agent adoption touch the files loop-idle-waits holds (host.cc, service.cc).
4. D125 (A4) is the largest piece and cannot start without 2.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | docs/mechanisms/server-composition.md (authority row and deviations; relay and agent pending) |
| A2 | pending: d7a901 (daemon tests), then the sigterm test; authority only |
| A3 | pending: not run (blocked on the pin wave) |
| A4 | not started (blocked on files) |

## Result

Not ready for review.

## Rulings I made

- Authority exit codes: a bad option value exits EX_USAGE (note step 1); other errors still exit 1.
- Defaults max_clients 1024, max_in_flight 1024, idle_timeout_ms 75000 (the note's Settled 5): the authority gains a 75 s idle close it did not have.
- The listening line is logged when http starts, not at ready, because the existing daemon tests read the port from it while the parent still waits.
- Kept the 5 ms leader wait as a `sleep_for` inside `LeaderComponent::start`, recorded as a deviation ending with loop-idle-waits.
