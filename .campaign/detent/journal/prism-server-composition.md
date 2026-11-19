# prism-server-composition: journal

State: ready for review
Quest: .campaign/detent/quests/prism-server-composition.md (the lead's; never edit it)

## Doing

All jobs and GitHub CI run 44634843777 passed at prism:805acc1.

## Next

1. Review (lead launches it).

## Blocked on

Nothing. msg (lead's answers): land now; the three library-gated steps go to quest
prism-server-composition-limits, named in ARCHITECTURE.md's deviation rows; README.md and
AGENTS.md now say `--port` (prism:805acc1); the lead tells the Xapiand lead about the note's row;
server-composition-adoption is not a dependency.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | `ARCHITECTURE.md` "How `main` is put together": the Mains row by step, options, deviations with reasons naming prism-server-composition-limits (prism:805acc1) |
| A2 | 261119-150720-9d025e (prism:805acc1), 261119-150056-8de58f (prism:4eb7074); fails without: 261119-150116-cb39e6 (grace 0 cuts the in-flight response) |
| A3 | 261119-150718-559dc2 (prism:805acc1); CI run 44634843777 green |

Local run (not evidence): `load: 14867 whole responses, 0 cut; exit 0 866 ms after SIGTERM`.

## Result

prism:4eb7074. `src/main.cc` composes main as the note's steps: options (prism.json via --config,
one name per flag and key, EX_USAGE/EX_CONFIG), `task::run_daemon` + `ProcessLock`,
`task::stop_signals`, constructor bounds (max_clients one bound for plain and TLS), TLS configure,
`asio_scheduler` on the main thread, `task::lifecycle(loop, signals, {bound, 5 s}, ready, http)`.
The 100 ms poll is gone. `src/test/lifecycle_test.cc` (ctest `prism_lifecycle`) is Prism's first
test. `ARCHITECTURE.md` documents it.

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
