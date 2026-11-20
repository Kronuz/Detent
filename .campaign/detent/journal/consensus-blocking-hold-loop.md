# consensus-blocking-hold-loop: journal

State: waiting on jobs 261119-230258-affdb7 261119-230259-564331 261119-230308-56689f 261119-230309-5599f1
Quest: .campaign/detent/quests/consensus-blocking-hold-loop.md (the lead's; never edit it)

## Doing

Jobs queued at consensus:5fc2ba7 (test commit, branch head) and the mutant consensus:aed7674.
CI pushed on quest/detent/consensus-blocking-hold-loop.

## Next

1. When jobs finish: read 261119-230259-564331's log and confirm it failed on the latency
   check ("within 50 ms"), not a build error (exit 2 would also count as nonzero).
2. Paste `campaign brief`'s Evidence table; check CI on the branch; State "ready for review".

## Blocked on

Nothing. A3 is decided as D131: `Hold`'s loop becomes required, in journal-hold-loop-required.
The census behind it: there are 3 `TurnDriver::Hold` constructions and all pass a loop (journal
turn_driver.h:315, journal blocking.h:91, consensus driver.h:1031). Detent constructs none.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261119-230258-affdb7 (pass, consensus:5fc2ba7); 261119-230259-564331 (--fails-without, mutant consensus:aed7674) |
| A2 | 261119-230308-56689f (TSan: consensus_driver incl. entry_wake_threads, blocking_entry, waiter_threads); suite 261119-230309-5599f1; CI on the branch |
| A3 | [decision] D131 (Hold's loop required; journal-hold-loop-required) |

## Result

The fix itself (the entry's `Hold` gets `scheduler_ref::of(scheduler)`) already landed in
consensus:08cdf68 with consensus-worker-release; review N1 read the pre-fix line 1011. This quest
adds `test/driver_test.cc` `entry_wake_threads`: a journal group sealed beside the Driver's on the
host's turn driver; another thread opens its record and calls `wake()` while `consensus::run`
sleeps toward a 2 s deadline; each of 20 rounds must return within 50 ms of the wake. The
mechanism is journal's docs/mechanisms/turn-seal.md.

## Rulings I made

- No driver.h change on the branch: the fix is on main. The `--fails-without` run is at a
  mutant commit, consensus:aed7674 on local branch `mutant/detent/consensus-blocking-hold-loop-deaf`
  (child of the test commit, the `Hold` without its loop), pushed to
  `refs/mutants/consensus-blocking-hold-loop/deaf-hold`.
- The case lives in driver_test.cc (the quest's file set) though blocking_entry_test.cc holds the
  other blocking-entry cases; driver_test.cc's header already claims D5 coverage.
- The wake source is a foreign sealer on the shared turn driver: consensus itself never calls
  `TurnDriver::wake()` (its `wake()` calls are transport deadlines), so a host's other journal
  user is the cross-thread waker.
- Per journal review N2: the case breaks on the first round over 50 ms, so the mutant costs one
  round (at most the 2 s deadline's wheel pass), and the deadline is 2 s, not 10 s.
- No journal repin: nothing here needs journal:bb114f1.
