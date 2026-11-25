# deps-workpool-repin: journal

State: waiting on jobs 261116-195652-960b07 (probe), 261116-195705-9ed96d (TSan repro, branch), 261116-195707-ca2128 (TSan repro, main ce7faa41)
Quest: .campaign/detent/quests/deps-workpool-repin.md (the lead's; never edit it)

## Doing

A1-A4 met (see Evidence). Landing is blocked by CI linux-tsan (run 44007470799, both attempts):
`test_local_paged_publication_artifacts` aborts ("invalid paged artifact operation accepted"), and attempt 2
also failed `test_system_managed_all-down` (authority member 2 refused connections for the whole 10 s drain).

Findings so far:
1. Paged test is pre-existing on main, not the repin: it failed in linux-tsan on main at 39b5314a (11-13),
   ea3d591e (11-13), 06fa2a03 (11-15), c02504e9 (11-16), ce7faa41 (11-16), all with logger f61d8df and
   scheduler, each passing on rerun; this branch at 881b188f (same logger/workpool/wheel pins) passed
   linux-tsan (run 43921954698). Scan: 200 CI runs since 11-11, failed logs grepped. Rate on main: the
   paged test failed 5 of 73 completed linux-tsan jobs (65 passed, 8 failed for any reason), about 7%;
   on this branch 2 of 3 (881b188f passed). Two in a row at 7% is about 0.5%, so the repro jobs compare
   the local rate at both commits before calling the branch rate unchanged.
2. Cause (from code, probe pending): `PagedDeltaPlanBuilder::take()` resets `work_`, so the builder's Work
   owner (leaves, overlay, finalizer scratch, and the pin on the input delta's RetiredInput) retires on the
   RetirementRuntime worker thread, releasing `account` bytes after the caller returns. The test's pressure
   branch (`account->admit(limit - used)` right after the plan block) can run before those frees; the frees
   then reopen headroom and `populate` (a few small allocations) succeeds, so `rejects<StoragePressure>`
   (line 69) throws. Timing only; TSan widens the window. The product behaves correctly.
3. Fix (test only, tests/detent_local/test_paged_publication_artifacts.cc, outside this quest's file set):
   wait for the builder's scratch owners to retire before taking the pressure admission, as
   tests/detent_distribution/test_retirement_factory.cc:77 already does (`eventually(residents == N)`,
   then admit). Probe 261116-195652-960b07 measures resident counts and checks the fix against a
   forced window (PROBE_SLEEP_MS).
4. all-down: one failure in 200 runs (this branch, attempt 2). The member's output lands in
   `<repo>/.scratch/stage6/all-down-*/events.jsonl`, which CI's failure_artifacts patterns
   (build/tests/**/.scratch/**) never upload, so the CI run kept no member log. Repro jobs run it 5x at
   both commits and dump member output on failure.

## Next

1. Read the three jobs. Probe: which line accepts, residents before/after take, fix/no-fix failure counts.
   Repro: paged and all-down failure counts at branch vs main.
2. Write the Blocked-on entry with the exact fix diff for the lead; settle all-down (flake rate on main or
   a member crash with its log).

## Blocked on

Pending the probe: the paged test fix is outside the file set (tests/**/*.cc); it will go here with the diff.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | pins job 261116-094703-763b98 (done, exit 0); manifest, CMake and check_pins grep job 261116-185753-a4efde |
| A2 | native suite 261116-094703-efabe8 at 7712f4eb (includes `test_logging` and every daemon test that logs); logger's own suite at 6e7ab9e (logger-burst-cost landed), 261115-211133-54acbc (`logger_deferred`, `_stress`, `_ties`, `logger_queue`, `logger_wake`, `logger_exit`, transcript golden) |
| A3 | grep job 261116-094719-348619 (allow-list below) |
| A4 | `benchmarks/logger_ab.py` over logger_bench (logger bench/lg.cc, producer cost per L_INFO call: burst, back to back, end-to-end; 1 and 4 producers), the same lg.cc built at logger:f61d8df (the old pin, binary lg-before, job 261115-213951-ab003c) and logger:907d27f (new pin, lg-after, job 261115-213955-6ce382); 30 paired rounds under exclusive: A/A 261116-185810-43f708, A/B 261116-185811-e33c44. Numbers: see Result when the jobs finish |

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
