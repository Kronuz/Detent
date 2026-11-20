# journal-blocking-wake: journal

State: blocked on lead: add include/journal/blocking.h and docs/mechanisms/turn-seal.md to the file set (jobs 261119-165717-173c61, 261119-165726-bcdfbd queued)
Quest: .campaign/detent/quests/journal-blocking-wake.md (the lead's; never edit it)

## Doing

Fix is in turn_driver.h (journal:0f5afcb); the entry must hand its scheduler to Hold, which is
blocking.h, outside the file set. Waiting on the ruling below.

## Next

1. On the ruling: blocking.h line 91 becomes
   `TurnDriver::Hold hold(turns, TurnDriver::Holder::entry, task::scheduler_ref::of(scheduler));`
   and its run() comment names a wake() among the posts that cut the sleep short;
   turn-seal.md:222-225 ("The blocking entry sees it at its next pass; wake() posts nothing
   there...") says the wake posts to the entry's scheduler and ends its sleep.
2. `campaign verify journal-blocking-wake --for A2` (suite), TSan job on journal_driver
   (entry_wake_threads), CI on the pushed branch.
3. Ready for review (correctness lens).

## Blocked on

Lead: the file set. The driver cannot learn the entry's scheduler from inside turn_driver.h:
journal::run constructs `Hold(turns, Holder::entry)` with no scheduler, and every driver call
the entry makes between passes (idle, commit) runs outside the scheduler, so task::current() is
null there. And turn-seal.md:222-225 documents the old behavior, so it is stale after any fix.

Options:
1. (Recommended) Add include/journal/blocking.h and docs/mechanisms/turn-seal.md to the files.
   One line in blocking.h passes the scheduler to Hold; both holders register their loop the
   same way, and wake() posts to whichever holds. Explicit and complete.
2. Keep the file set: link() records the first waiter's home as the entry's loop. Covers a
   durable() waiter (the quest's case) but not a wake while the task waits on something else;
   implicit, and the doc is still stale (docs still needs adding).

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261119-165717-173c61 (--fails-without, test-only commit journal:449553d); passing run waits on the ruling. Experiment 261119-165726-bcdfbd: fix + the blocking.h line applied by sed, full driver suite. |
| A2 | not yet |

## Result

Not yet.

## Rulings I made

- Test lives in test/driver_test.cc (entry_wake_threads), a case of the existing journal_driver
  CTest entry, so CMakeLists.txt (outside the file set) needs no line. It fails, not hangs,
  without the fix: a 10 s deadline per round, break on the first slept-out round.
- The entry's wake posts a no-op call (no allocation), not run()'s refcounted Wakeup: the entry
  re-reads idle() on every pass, and a Wakeup left queued in a blocking_scheduler that is
  destroyed undriven would leak.
- run() now registers its loop through Hold too (one place sets loop_), and Loop keeps only
  wakeup_. Between ~Loop and ~Hold a wake posts the no-op to run()'s still-live loop: harmless.
