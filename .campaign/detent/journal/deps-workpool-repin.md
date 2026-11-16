# deps-workpool-repin: journal

State: ready for review (A4 jobs 261115-214405-b29364 A/A and 261115-214410-6f1bd9 A/B still to finish; A1-A3 and native done)
Quest: .campaign/detent/quests/deps-workpool-repin.md (the lead's; never edit it)

## Doing

Work is committed at Detent:8d7561a3. A1-A3 and the native suite are green. A4: the first benchmark failed
for a reason the repin did not cause; replaced by the logger's own producer-cost bench (below). Only jobs remain.

## Next

1. When 261115-214405-b29364 (A/A) and 261115-214410-6f1bd9 (A/B) finish, read `report.json` in
   `$HOME/.scratch/deps-workpool-repin/lgaa-1` and `lgab-1` (the job logs end with the per-metric table)
   and copy the median ns, ratios and the A/A band into the Evidence row; a ratio above the A/A band is a
   regression to report to the lead, not to bury.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | pins job 261115-211006-0337c2 (done, exit 0); manifest, CMake and check_pins grep job 261115-211114-08a706 |
| A2 | native suite 261115-211005-c53012 at 7712f4eb (includes `test_logging` and every daemon test that logs); logger's own suite at 907d27f, 261115-211133-54acbc (`logger_deferred`, `_stress`, `_ties`, `logger_queue`, `logger_wake`, `logger_exit`, transcript golden) |
| A3 | grep job 261115-211119-30f359 (allow-list below) |
| A4 | `benchmarks/logger_ab.py` over logger_bench (logger bench/lg.cc, producer cost per L_INFO call: burst, back to back, end-to-end; 1 and 4 producers), the same lg.cc built at logger:f61d8df (the old pin, binary lg-before, job 261115-213951-ab003c) and logger:907d27f (new pin, lg-after, job 261115-213955-6ce382); 30 paired rounds under exclusive: A/A 261115-214405-b29364, A/B 261115-214410-6f1bd9. Numbers: see Result when the jobs finish |

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

0. A4 benchmark: `benchmarks/logging_compare.py` cannot run at current detentd, before or after the repin: A/A and A/B
   (261115-211055-4ef209, 261115-211059-ed1a5a) exit with `detentd: unknown argument: --persist-path` (the
   daemon is now `detentd --authority CONFIG`, TLS, no `/v1/snapshot`), and a probe of the before binary
   at `-vv` over a single-voter authority (HTTPS, 3 `/v1/health` polls) logs only its start and shutdown
   lines: detentd no longer logs per HTTP request, so the script's workload (verbose HTTP logs under
   conditional polling) does not exist. Fixing it means inventing a logging workload in the daemon, not a
   few edits. The script stays as the October record. Used instead: the logger's own producer-cost bench
   (an existing logging-heavy bench, built from one lg.cc against both logger commits), wrapped by
   `benchmarks/logger_ab.py`; the logger is the changed component, and the bench exercises exactly its
   producer path and LOG-thread drain.

1. stash is dropped with scheduler: only scheduler declared it (checked over every pinned Kronuz library's
   FetchContent_Declare calls), D72 retires it, and nothing in Detent uses it.
2. wheel is added to the manifest: the logger declares it (wheel:bfd72bc), and a declared shared dependency
   not in the manifest escapes `check_pins`.
3. Logger commit: 907d27f is logger main's head (it is `Move to workpool` on top of a07f3fe's line); it
   links `workpool::workpool` and pins workpool:640c66b.
4. A4 uses 3 pairs of 50,000 polls (exclusive jobs are capped at 10 minutes, the script runs all three
   cases in one process), with an A/A job (the before binary against itself) for the band.
5. `docs/LOGGING.md` item 7 removed rather than reworded: the logger now pins every dependency by commit.
