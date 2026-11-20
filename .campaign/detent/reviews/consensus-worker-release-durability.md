# consensus-worker-release review: durability

Commit: consensus:5de6c78011b8a805de530a645261ed8de495eb2a
Verdict: CHANGES
Date: 2026-11-19

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

This replaces the review at consensus:4fe7df2 (CHANGES, F1 and F2). Its F1 (malformed jobs) is
answered: the jobs below are tagged, ran at 5de6c78 and reached their tests. Its F2 is fixed for one
Driver's life. It is not fixed for the Host's life, which D128 and DRIVER.md promise; that is F1 below.

## Evidence I relied on

All at consensus:5de6c78. I read each job's log.

- 261119-170644-cac82f: failed_close passed, 15 checks.
- 261119-170646-a56176: the reopen mutant removes the `in_doubt_` refusal (driver.h:419). It
  reached the test and failed 4 checks. The live recover is not refused. The restarted host loses
  the group ("the turn log lost this group"). Acknowledged `a` and `x` are both lost. The test fails
  without the fix.
- 261119-170647-c66dad: close_crash ran 200 seeds, 19,512 power cuts, 17,322 table closes and
  30,519 releases, with 0 failures. It includes the new every-call-completes check.
- 261119-170649-5a5b1e: the loop-throw mutant removes `shutdown()` from `catch (...)`
  (driver.h:519). It reached the test and failed exactly one check, "every call completes once the
  loop ends: seed 1 cut 53" (1 of 366). The check fails without the loop fix.
- 261119-170650-eb7f80: ASan+UBSan on the driver_close tests passed. This is macOS, so there is no
  LSan. The 66-frame leak is checked only by CI's linux-asan-ubsan.
- 261119-170651-ca2fc5: the suite passed.

## What I checked myself

I read `git diff 4fe7df2..5de6c78` and the whole throw path: `loop()` (driver.h:502-520), `step()`'s
catch (driver.h:672), `shutdown()` (driver.h:979), `consensus::run` (driver.h:1021-1064) and
journal's `entry_root` (journal blocking.h:73-79). I also compared the journal pin (be8222c) with
journal-close-in-doubt (journal:8c92ed9, landed; be8222c is its parent).

Probes live in `.scratch/detent/review-consensus-worker-release-durability/` in the Detent home.
Both were still queued when I wrote this.

- 261119-184141-7487c2 `second_driver.sh`, at the branch's journal pin. It patches in a
  `second_driver` case (`second_driver.py`). A close's table commit fails. The first Driver's run
  ends with no row left. A second Driver is built over the same live Host and recovers the id,
  acknowledges `x`, syncs the directory and cuts power. The case asserts the refusal and that every
  acknowledged record survives. By reading, it fails (F1).
- 261119-184142-2f5a77: the same probe with journal repinned to 8c92ed9. By reading, the Host's
  `hold` refuses with EIO (journal host.h `hold`, `GroupEntry::in_doubt`), so it passes.

Answers to the lead's four questions:

1. **Waiting calls on a throw.** A caller completed with `cancelled` can have a durable record. Its
   record may have been staged into the turn before the throw, or already be durable while its watch
   completion was not yet handled. `cancelled` claims nothing either way. `shutdown()` only claims
   waiters as cancelled (driver.h:979-992), and the claim protocol has one winner, so no caller is
   told "placed" for a record that is not durable. A blind retry can therefore duplicate the
   record. That is the documented stop contract, which this change extends to a throw. Consensus
   has no request deduplication. It is not a durability loss. DRIVER.md's wording overstates it (N2).
2. **After the throw.** The fix writes nothing. `shutdown()` cancels the shutdown token and posts
   waiters. close_crash's 200 seeds recover every cut with every acknowledged record and nothing
   unattempted, so no on-disk state the throw leaves is misread at a restart, and no table close
   replays covered records. In memory, `in_doubt_` lives in the Driver and dies with it. A reopen
   through another Driver over the same live Host misreads the table (F1). `stopping_` stays set
   after a throw, so a reactor `run()` started again on the same Driver cancels every call. Only
   `consensus::run` rearms (N3).
3. **F2.** It is fixed while the Driver that saw the failed close lives (cac82f, and mutant a56176
   kills the fix). It is not fixed per D128, which says "until the Host reopens" (F1).
4. **The leak.** 5a5b1e fails only the every-call-completes check. That check tracks the propose,
   checkpoint and close calls of close_crash's rounds, which are the frames LSan reported at
   driver_close_test.cc:777. Whether the LSan leak is gone rests on CI, which was queued at 5de6c78
   (run 44651118073).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | defect | **The refusal lasts the Driver's life, not the Host's.** `in_doubt_` is a Driver member (driver.h:1007, :941). The pinned journal (be8222c) restores the entry to open after a failed commit and keeps no mark, so a second Driver built over the same live Host recovers the id and appends past the closed base. After a later directory sync and a power cut, the restart holds the group closed, and every record acknowledged after the reopen is lost. This is the old F2 through a second Driver. `Driver(Seams, Sink&)` does not refuse a Host that another Driver used, and DRIVER.md:137 promises `close_in_doubt` "until the host restarts". journal-close-in-doubt (journal:8c92ed9, landed; the pin is its parent) already keeps the mark in the Host, as D128 rules. | Reading. Probe 261119-184141-7487c2 (expected to fail) against 261119-184142-2f5a77 (repinned, expected to pass), both queued. | Repin journal to at least 8c92ed9; A6 already asks for journal's main. Then either drop `in_doubt_` and map the Host's EIO from `hold` to `close_in_doubt` (one mechanism, in journal), or keep `in_doubt_` only to name the error. Add `second_driver` (second_driver.py) as a driver_close_test case. It fails at the current pin. |
| N1 | no | gap | failed_close injects only the first `sync_directory` failure after the replace (driver_close_test.cc:873). The earlier probe covered the 1st, 2nd and 3rd. The mark is set on any exception, so the outcome should not depend on which sync fails. | driver_close_test.cc:873 | Loop `fail_sync` over 1..3. |
| N2 | no | nit | DRIVER.md:140 says that after a throw "a staged record is still written". After a power-cut throw it may not be. `cancelled` means the outcome is unknown, and a caller that retries may duplicate the record. | DRIVER.md:140 | Say "may still be written; the outcome is unknown". |
| N3 | no | defect | `consensus::run` never reads `loop_result.error` (driver.h:1030-1062). A throw that ends the loop in the blocking entry reaches the caller as the work's `task::cancelled`, or as success when the work absorbs it. The Driver is then rearmed (driver.h:1060). Before this commit the same case surfaced as `stalled`. Either way the error passes silently. | driver.h:1030-1062, journal blocking.h:73-79 | Rethrow `loop_result.error` first and leave the Driver stopped. |
| N4 | no | gap | Pre-existing: `step()` fences only on `std::exception` (driver.h:672). A non-standard throw, such as the crash model's PowerLoss or a Sink's `throw 42`, leaves its group half-stepped and unfenced. A rearmed blocking entry, or a second `run()`, steps it again. The new `catch (...)` could fence every group before `shutdown()`. | driver.h:672, :519 | Optional: fence each group in `loop()`'s `catch (...)`. |
| N5 | no | nit | After a throw, `loop()` rethrows with IO still in flight, and the completions capture `this` and `&g` (driver.h:950, :967). DRIVER.md:75 allows `~Driver` only after `run()` returned and every group closed, and the life after a throw is unstated. This predates the commit, which only cancels that IO sooner. | driver.h:946-971, DRIVER.md:75 | Document: after a throw, drain the scheduler before `~Driver`. |

The old review's F4 and F5 nits stand unchanged. Its F3 is answered by failed_close.

## Not run

- Neither probe had a result when I wrote this. F1 rests on reading the pinned journal's
  `close_group` and `update` path, which the earlier probe 261119-140902-4049e0 (at 4fe7df2) and the mutant
  a56176 show losing records once a live reopen is allowed.
- CI at 5de6c78, which was queued: linux-asan-ubsan (the LSan leak), linux-tsan and the matrix.
- A local TSan run at 5de6c78. The A4 TSan job ran at e74fccf. The loop change adds no thread, but
  the job ran other code.
- A throw ending `consensus::run`'s loop: no test makes a non-standard exception escape there (N3).
- Owned (non-shared) Stores. Multi-voter rows.
