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

A3, the lead's decision. Every `TurnDriver::Hold` construction, at journal:bb114f1,
consensus:adb918f and Detent:2b2d70c3 mains:

1. journal `include/journal/turn_driver.h:315`, `run()`'s `Loop`: passes `task::current()`.
2. journal `include/journal/blocking.h:91`, `journal::run`: passes `scheduler_ref::of(scheduler)`.
3. consensus `include/consensus/driver.h:1031`, `consensus::run`: passes `scheduler_ref::of(scheduler)`
   (since consensus:08cdf68).

Detent constructs none; no journal or consensus test constructs one. Three of three pass a loop;
none uses the `loop = {}` default.

Options:
1. Make `loop` required in `Hold`, and refuse an empty one (`std::invalid_argument`), as
   `durable()` refuses a scheduler without `post_call` since journal:485cd23. Costs zero call-site
   edits; a future deaf holder fails to compile or throws at the hold instead of losing wakes.
2. Keep the default: the deaf path stays silent, caught only by a cross-thread wake test like
   this one.

Recommendation: 1, as a small journal quest (turn_driver.h plus a refusal test). Greenfield, no
caller pays, and one obvious way to build a Hold. Consensus needs no change beyond its next pin.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | 261119-230258-affdb7 (pass, consensus:5fc2ba7); 261119-230259-564331 (--fails-without, mutant consensus:aed7674) |
| A2 | 261119-230308-56689f (TSan: consensus_driver incl. entry_wake_threads, blocking_entry, waiter_threads); suite 261119-230309-5599f1; CI on the branch |
| A3 | [decision] under Blocked on; the lead records it in PLAN.md |

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
  (child of the test commit, the `Hold` without its loop), not pushed.
- The case lives in driver_test.cc (the quest's file set) though blocking_entry_test.cc holds the
  other blocking-entry cases; driver_test.cc's header already claims D5 coverage.
- The wake source is a foreign sealer on the shared turn driver: consensus itself never calls
  `TurnDriver::wake()` (its `wake()` calls are transport deadlines), so a host's other journal
  user is the cross-thread waker.
- Per journal review N2: the case breaks on the first round over 50 ms, so the mutant costs one
  round (at most the 2 s deadline's wheel pass), and the deadline is 2 s, not 10 s.
- No journal repin: nothing here needs journal:bb114f1.
