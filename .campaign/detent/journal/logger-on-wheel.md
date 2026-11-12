# logger-on-wheel: journal

State: working
Quest: .campaign/detent/quests/logger-on-wheel.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Jobs queued at logger:be2106c / 11644a5: suite 261112-140315-039e6c (A1), mutant
261112-140328-3415e1 (A1 --fails-without), TSan 261112-140332-797330 (A2), grep
261112-140334-59109a (A4), A3 exclusive: p2 P=1 261112-140412-ee682f, P=4 -140415-a16bfe,
P=14 -140418-0c17c8, p3 261112-140421-86b4c3.
Local light run (no evidence): logger_deferred_test timed wake median 0.385 ms p99 1.004 ms;
lateness min 0.010, median 0.462, p99 1.476, max 1.576 ms (471 lines); stress 21,017 lines ok.

## Next

1. On A3 results: write docs/perf/wheel-port.md (rows, verdict), CHANGELOG numbers; Result.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | <pending> |
| A2 | <pending> |
| A3 | <pending> |
| A4 | <pending> |
| A5 | `ARCHITECTURE.md`: "Deferred lines ride the LOG thread's wheel" |

## Result

<pending>

## Rulings I made

- The queues' merge heap (`heads`, rebuilt each pass, holds nothing between passes) is not a
  deadline structure; A4's grep allows `_heap` calls only on `heads`.
- logger.h includes threadpool's `thread.hh` (what it uses) instead of `scheduler.h`; the scheduler
  pin stays because it brings threadpool and test/frozen uses the scheduler.
- The wheel_ab option no longer declares its own wheel pin (50665c8): the library's bfd72bc serves it.
- A3 harness: bench/** is outside the file set, so docs/perf/wheel-port.sh runs bench/wheel_ab's
  binaries (d/CMakeLists build) with row-d's labels swapped (A, A2 = arm D; D = library), so
  summarize.py --paired's r is library / arm D; exits 1 unless every row is "no worse".
- A3 jobs: pattern 3 is one job (P=1,4,14); pattern 2 is one job per P, because a pattern 2 row
  takes ~275 s (Arm D, fixed) and an exclusive job is capped at 10 min (--long jobs prove nothing
  before landing).
- Lateness bound in test/deferred.cc: median <= 1 tick + measured timed-wake median + 0.5 ms, p99
  <= 1 tick + timed-wake p99 + 2 ms; skipped under TSan (never-early, once, order still checked).
