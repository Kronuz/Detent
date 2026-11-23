# journal-blocking-wake: journal

State: waiting on jobs 261119-165717-173c61 261119-165904-295eae 261119-165908-3668c1 261119-165912-fca4c3
Quest: .campaign/detent/quests/journal-blocking-wake.md (the lead's; never edit it)

## Doing

Jobs at head journal:9e11713 (fix in turn_driver.h + blocking.h passes its scheduler to Hold;
turn-seal.md describes the wake under both holders). CI runs on the pushed branch.

## Next

1. Read the jobs; on green, State "ready for review" (correctness lens) and fill Result.

## Blocked on

Nothing. msg (lead, file set): added blocking.h and turn-seal.md; took option 1, journal:9e11713.
Cancelled the sed experiment 261119-165726-bcdfbd (not evidence).

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261119-165717-173c61 (--fails-without at test-only journal:449553d); 261119-165904-295eae (entry_wake_threads at head) |
| A2 | 261119-165912-fca4c3 (suite); 261119-165908-3668c1 (TSan: journal_driver and turn_seal thread cases); CI on the branch |

## Result

Pending jobs. Changed: include/journal/turn_driver.h (Hold carries the holder's loop; wake()
posts to it), include/journal/blocking.h (passes its scheduler), docs/mechanisms/turn-seal.md
("Asking for a turn"), CHANGELOG.md, test/driver_test.cc (entry_wake_threads).

## Rulings I made

- Test lives in test/driver_test.cc (entry_wake_threads), a case of the existing journal_driver
  CTest entry, so CMakeLists.txt (outside the file set) needs no line. It fails, not hangs,
  without the fix: a 10 s deadline per round, break on the first slept-out round.
- The entry's wake posts a no-op call (no allocation), not run()'s refcounted Wakeup: the entry
  re-reads idle() on every pass, and a Wakeup left queued in a blocking_scheduler that is
  destroyed undriven would leak.
- run() now registers its loop through Hold too (one place sets loop_), and Loop keeps only
  wakeup_. Between ~Loop and ~Hold a wake posts the no-op to run()'s still-live loop: harmless.
