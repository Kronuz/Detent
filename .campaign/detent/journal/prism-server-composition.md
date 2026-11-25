# prism-server-composition: journal

State: waiting on jobs 261119-155134-1b2ec6 261119-155135-0d9393 261119-155137-da8de7 261119-155139-d94e2b (and CI run 44641100887)
Quest: .campaign/detent/quests/prism-server-composition.md (the lead's; never edit it)

## Doing

F1, F2, N1, N5 fixed at prism:cf3b651 (local: stalled exit 0 at 2008 ms, bound 2000 ms; missing
TLS file detached exits 78). Jobs and CI run 44641100887 queued at prism:cf3b651.

## Next

1. When the jobs and CI pass, update Evidence to prism:cf3b651 and set State ready for review.

## Blocked on

Nothing. msg (lead's answers): land now; the three library-gated steps go to quest
prism-server-composition-limits, named in ARCHITECTURE.md's deviation rows; README.md and
AGENTS.md now say `--port` (prism:805acc1); the lead tells the Xapiand lead about the note's row;
server-composition-adoption is not a dependency.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | `ARCHITECTURE.md` "How `main` is put together" at prism:cf3b651, deviations naming prism-server-composition-limits |
| A2 | pending at prism:cf3b651: 261119-155135-0d9393; fails without: 261119-155137-da8de7 (60 s grace, F1), 261119-155139-d94e2b (zero grace) |
| A3 | pending at prism:cf3b651: 261119-155134-1b2ec6; CI run 44641100887 |

Local run (not evidence): `load: 14867 whole responses, 0 cut; exit 0 866 ms after SIGTERM`.

## Result

prism:4eb7074. `src/main.cc` composes main as the note's steps: options (prism.json via --config,
one name per flag and key, EX_USAGE/EX_CONFIG), `task::run_daemon` + `ProcessLock`,
`task::stop_signals`, constructor bounds (max_clients one bound for plain and TLS), TLS configure,
`asio_scheduler` on the main thread, `task::lifecycle(loop, signals, {bound, 5 s}, ready, http)`.
The 100 ms poll is gone. `src/test/lifecycle_test.cc` (ctest `prism_lifecycle`) is Prism's first
test. `ARCHITECTURE.md` documents it.

## Review findings (correctness, prism:805acc1)

- F1 (gap): `stalled_test` in src/test/lifecycle_test.cc: a peer that never reads its 4 MiB
  response, `--shutdown-timeout-ms 2000`, SIGTERM; prism must exit 0 within bound + 1 s. Mutant:
  grace 60 s (the reviewer's mutant_long_grace.sh edit), submitted --fails-without.
- F2 (docs): README stop paragraph, the usage text (src/main.cc kUsage), ARCHITECTURE's Signals row
  and the deviation row now say: a response being written gets the grace then is cut; a request
  still being received is reset with no response; a second signal does nothing yet; fixed by
  stop_accepting in prism-server-composition-limits.
- N1: fixed. The TLS certificate and key are checked as readable before run_daemon, so a missing
  one exits 78 detached as in the foreground. A malformed PEM still fails after the fork (1 when
  detached); left there to keep the note's step order (process before TLS).
- N2: folded as one clause, not a code change: the deviation row and the Stop row now say a cut
  response exits 0 with nothing reported and EX_TEMPFAIL does not arise for Prism yet. Leaving
  the Stop row's EX_TEMPFAIL claim would have kept a false statement. The reporting itself waits on
  reactor reporting forced sessions (prism-server-composition-limits).
- N3: skipped. "already running" exiting 0 into the log is Detent's convention
  (detent_authority/main.cc:156); one shape across daemons, the owner's call.
- N4: skipped. `--port 0` sharing prism-0.pid follows Detent's pid name (role-PORT); naming it after
  the bound port would need the port before the lock, which is after start.
- N5: fixed. BENCHMARKS.md:133 uses `--port 8880`.
- N6: skipped. Optional: message wording, `-q` as a flag-only shorthand, and `--color`'s lenient
  values are unchanged from before this quest.
- N7: skipped. Optional. Under the current stop, a request queued on a keep-alive connection is
  reset by design (F2's documented behavior), so counting it would assert the behavior
  prism-server-composition-limits adds; that quest's test should count it.

## Rulings I made

1. Pins: task 0316bef (includes 3ce0f0c, fb32493, 4b6a096); http to its head 58d4188, whose handlers
   are task coroutines, so reactor, http and Prism build on one task pin; logger 529ae76 and http-log
   5e3df31 at their heads (http-log's head builds on task-based http). Prism's own reactor pin is
   dropped: http's (ab69cf2) has the TLS Server role.
2. prism.json is read with RapidJSON at Detent's pin (24b5e7a), headers only.
3. The configuration file comes only from `--config`: Prism installs nowhere, so no default
   directory (deviation row in ARCHITECTURE.md).
4. A second instance prints "already running" and exits 0, as detent-authority does.
5. The grace is min(5 s, shutdown_timeout_ms): http's legacy stop blocks the loop during the drain,
   so the lifecycle's timer cannot cut it; this keeps the bound.
6. Defaults per the note: max_in_flight 256 -> 1024 (Settled 5); the positional port becomes
   `--port` (C3); idle timeout 75 s now applies to plain and TLS.
7. The test lives in src/test/ (the file set is src/**) and makes its scratch under ctest's working
   directory.
8. The mutant (grace 0) exits 126 when its sed or build fails, so only the test's failure counts.
