# prism-server-composition: journal

State: waiting on jobs 261119-150055-92bbee 261119-150056-8de58f 261119-150116-cb39e6
Quest: .campaign/detent/quests/prism-server-composition.md (the lead's; never edit it)

## Doing

Jobs at prism:4eb7074: build gate (A3), the lifecycle test (A2), the test at a zero-grace mutant
(A2, --fails-without). CI runs on the pushed branch quest/detent/prism-server-composition.

## Next

1. Read the jobs and CI; paste `campaign brief`'s table under Evidence; then ready for review of
   the task-side steps, with the library-side steps under Blocked on.

## Blocked on

Lead: three of the outcome's steps need library work that has not landed.
1. One limits struct: `http::Limits` is Xapiand quest http-limits (planned, after
   reactor-server-composition).
2. `raise_open_files` and `tls::server_context`: Xapiand quest reactor-server-composition (in review,
   unlanded).
3. Http's `stop()` closing only listeners, with in-flight requests finishing instead of being
   cancelled: http-limits' `stop_accepting()` over reactor's stop phases.
Options: (a) land this now (process, signals, options/config, lifecycle, no polling) and file a
follow-up prism quest after http-limits lands; (b) hold this quest open until then. Recommend (a):
small and landed, and the remaining change is the constructor, one call and Http's stop/drain.
Also: README.md and AGENTS.md still show the positional port (`./prism 8880`), now `--port`; they
are outside the file set. Recommend adding both to the file set (or the follow-up's).
Note owner (Xapiand lead): the note's "No daemon mode (Prism) ... Stays" row and Prism's Options
and Mains cells are outdated by this change; ARCHITECTURE.md has the new row.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | `ARCHITECTURE.md` "How `main` is put together": the Mains row by step, options, deviations with reasons (prism:4eb7074) |
| A2 | pending: 261119-150056-8de58f (test), 261119-150116-cb39e6 (zero-grace mutant, --fails-without) |
| A3 | pending: 261119-150055-92bbee (build gate); CI on the pushed branch |

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
