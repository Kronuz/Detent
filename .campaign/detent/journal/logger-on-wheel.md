# logger-on-wheel: journal

State: working
Quest: .campaign/detent/quests/logger-on-wheel.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Port committed at logger:a07f3fe (arm D's logger.{h,cc} minus WAB_STATS; wheel pinned bfd72bc;
test/deferred.cc + test/deferred_mutant.sh). Next: local build/run under a light hold, then jobs.

## Next

1. Build and run logger_deferred_test locally (light hold); calibrate bounds.
2. Jobs: suite (A1), mutant --fails-without (A1), TSan suite+stress (A2), grep (A4).
3. A3: one exclusive job per pattern (p3, p2), P=1,4,14 x 40 rounds, production as candidate,
   arm D as reference (labels swapped into summarize.py --paired).

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
