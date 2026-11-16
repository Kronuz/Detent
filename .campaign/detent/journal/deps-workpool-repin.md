# deps-workpool-repin: journal

State: waiting on jobs 261115-211005-c53012 261115-211114-08a706 261115-211119-30f359 261115-211133-54acbc 261115-211055-4ef209 261115-211059-ed1a5a
Quest: .campaign/detent/quests/deps-workpool-repin.md (the lead's; never edit it)

## Doing

Work is committed at Detent:7712f4eb; only jobs remain (below). The sources use neither the scheduler
library nor the ThreadPool family (git grep of src/, include/, tests/, benchmarks/*.cc, tools/ finds
only asio::thread_pool, Python's ThreadPoolExecutor and asyncio.TaskGroup), so no message to the lead.

## Next

1. When the jobs finish: fill the A4 numbers (median CPU and RSS, before/after, with the A/A band from
   `$HOME/.scratch/deps-workpool-repin/aa-1/report.json` and `ab-1/report.json`), then set "ready for review".

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | pins job 261115-211006-0337c2 (done, exit 0); manifest, CMake and check_pins grep job 261115-211114-08a706 |
| A2 | native suite 261115-211005-c53012 at 7712f4eb (includes `test_logging` and every daemon test that logs); logger's own suite at 907d27f, 261115-211133-54acbc (`logger_deferred`, `_stress`, `_ties`, `logger_queue`, `logger_wake`, `logger_exit`, transcript golden) |
| A3 | grep job 261115-211119-30f359 (allow-list below) |
| A4 | benchmark `benchmarks/logging_compare.py` (daemon CPU, RSS and log volume of `detentd -v` under conditional polling; filtered, verbose-plain, verbose-rgb); builds 261115-211037-d13558 (before, 079444b1) and 261115-211040-bfbd6d (after, 7712f4eb); A/A 261115-211055-4ef209; A/B 261115-211059-ed1a5a |

## Result

Commit 7712f4eb: `cmake/dependencies.json` pins detent_logger at logger:907d27f (links
`workpool::workpool`, deferred lines on the LOG thread's wheel, D86), workpool:640c66b and wheel:bfd72bc
(both as the logger declares them; check_pins reports 0 mismatches); scheduler, stash and threadpool are
removed (D72). No CMake file or tool named them: Detent's build takes every pin from the manifest, so
`cmake/DependencyPins.cmake`, the CMake files and `tools/check_pins.py` (generic over the manifest) need no
edit. Docs: `docs/DEPENDENCIES.md` count (34 entries, 29 Kronuz), `docs/dev/superseded-libraries.md`
(threadpool -> workpool), `docs/LOGGING.md` (upstream proposal 7, pin dependency revisions, is met by the
logger's commit pins and removed), `CHANGELOG.md` Unreleased entry.

A3 allow-list (dated history; every hit is a dated record of what was true then): `.campaign/`,
`CHANGELOG.md`, `benchmarks/*.json`, `docs/measurements/`, `docs/design/` (dated design evidence, including
the frozen `async-drivers-bench/` that measures against stash and scheduler by design, D83),
`docs/status/`, `docs/reviews/`, `docs/DEPENDENCY_UPGRADES.md` (October upgrade log),
`docs/LIBRARY_ADOPTION_ARCHITECTURE.md` (October design), `docs/dev/superseded-libraries.md` (the 2026-11-11
sweep that records scheduler and stash as superseded; its one threadpool mention is renamed, and the job
fails if one returns).

The tell-the-Xapiand-lead step in the quest's Decisions is the lead's, after landing.

## Rulings I made

1. stash is dropped with scheduler: only scheduler declared it (checked over every pinned Kronuz library's
   FetchContent_Declare calls), D72 retires it, and nothing in Detent uses it.
2. wheel is added to the manifest: the logger declares it (wheel:bfd72bc), and a declared shared dependency
   not in the manifest escapes `check_pins`.
3. Logger commit: 907d27f is logger main's head (it is `Move to workpool` on top of a07f3fe's line); it
   links `workpool::workpool` and pins workpool:640c66b.
4. A4 uses 3 pairs of 50,000 polls (exclusive jobs are capped at 10 minutes, the script runs all three
   cases in one process), with an A/A job (the before binary against itself) for the band.
5. `docs/LOGGING.md` item 7 removed rather than reworded: the logger now pins every dependency by commit.
