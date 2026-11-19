# driver-apply-gap: journal

State: waiting on jobs 261119-044522-91dbd6 261119-044524-300372 261119-044528-fa2a93 261119-044534-b31c95..261119-044618-dbd1d0 261119-044634-1c8de7 261119-044634-3835e7
Quest: .campaign/detent/quests/driver-apply-gap.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Fix round 1 (review `.campaign/detent/reviews/driver-apply-gap-correctness.md`, CHANGES, F1
blocking). Code head consensus:ee03230 (main merged as consensus:5100305; pushed). Queued at
ee03230: suite 261119-044522-91dbd6; TSan 261119-044524-300372, ASan 261119-044528-fa2a93 (seed 399
regression and truncated_promotion); seeds 1-10000 x 3000 in 20 jobs 261119-044534-b31c95 ..
261119-044618-dbd1d0; reruns 261119-044634-1c8de7 (A4 doc grep), 261119-044634-3835e7 (A1 trace);
CI on the push.

What changed for the reviewer (consensus:9f7bc0c..ee03230, besides the main merge):
1. `include/consensus/driver.h`: `witness(g, index)` reads `w.membership_at(index)`, and the
   Committed branch passes the range's last index, so an appended but uncommitted promotion no
   longer diverts a range from the Sink.
2. `include/consensus/core.h`: `membership_at` moves from private to public, beside
   `committed_membership`, unchanged. `include/consensus/worker.h`: `membership_at(index)` forwards
   it like `membership()`.
3. `test/driver_test.cc`: `truncated_promotion`, a joiner learner (incarnation 4) receives term 1's
   entries 1..4 with the promotion to witness at 4 and commit 3, then term 2's no-op at 4 that
   truncates it; checks applied <= delivered after the first append, applied 4 = delivered 4, the
   learner role after truncation, and contiguous ranges.
4. `docs/DRIVER.md`: the invariant names its one exception: a range whose last index is in a
   membership that makes the node a witness (a committed entry no term can truncate); the Sink
   notice list says the same.

## Next

1. When the jobs pass: `campaign accept driver-apply-gap correctness --confirm F1=261119-044522-91dbd6`
   is the lead's; State "ready for review".
2. Lead: the file set (Blocked on).

## Blocked on

Lead: add `include/consensus/core.h` and `include/consensus/worker.h` to the file set. The ordered
fix needs `membership_at` from the Driver; Core kept it private and the Worker did not expose it.
The change is one moved accessor and one forwarding line. No active quest holds either file
(consensus-quiescence, consensus-worker-release, driver-pipeline and worker-pump-removal list them
and are planned). Recommendation: widen the set; the alternative, tracking membership inside the
Driver from Committed entries, has no starting value after `recover`.

Open, for the lead to rule (not fixed): the reverse case of F1. A witness whose removal is in force
at a range's last index is no longer a member there, so that range goes to its Sink, which holds no
entries. The old rule did the same, earlier (from the removal's append). Judging "witness at the
range's first entry or at its last" would cover it, since R16 keeps a witness from becoming a data
member; recommendation: a follow-up quest with a driver_test case, not this round.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 [manual] | Seed 399: the trace (`SIM_DRIVER_TRACE=1`, first read at consensus:40eeec7, before the fix) shows no Driver path advanced apply past delivery. Node 2 led term 6 (step 2749) and placed 6:120..6:126 while node 3 was down (booted step 2999); the heal loop's "healed" placed 6:126 (heal round 1). Node 3 (founded a Witness for odd seeds, driver_simulation.cc:276) won term 7 (round 37; its vote can only be node 1's, as node 2's log is longer); node 1 won term 8 (round 44); index 120 committed with a new entry (round 46), so 6:120..6:126 were replaced. The old loop re-proposed only on an error or no placement, so it waited 4,000 rounds on 6:126. Node 3's "application 0" is the witness rule. The reviewer's probe 261118-224440-51acaf checks the Driver's own `state().applied` against delivery after every step and heal round: 78,235,641 checks over seeds 1-10000 x 3000, none past delivery (this supersedes the trace awk check, F3). The real path the quest names exists outside the simulation's fixed membership: review F1, a learner whose promotion to witness is appended (core.h note_append) acknowledges ranges below it through `witness(g)` (driver.h, newest membership) and keeps that apply index when a later term truncates the promotion; reproduced by `truncated_promotion` (A2). Rerun of the trace at the head: 261119-044634-3835e7. |
| A2 | Seed 399: 261118-173937-ee5fda; fails without: 261118-173953-5ceffe. F1: 261119-044225-2f4e74 (truncated_promotion and all of driver_test, 132 checks, 0 failures, at ee03230); fails without: 261119-044228-4bb073 (newest-membership mutant: "applied 3, delivered 0", 2 failures) |
| A3 | at ee03230: TSan 261119-044524-300372, ASan 261119-044528-fa2a93, seeds 1-10000 x 3000 261119-044534-b31c95 .. 261119-044618-dbd1d0 (queued) |
| A4 [artifact] | `docs/DRIVER.md`, "The apply index never passes what the application was delivered" and its witness exception (consensus:ee03230); 261119-044634-1c8de7 greps it at the head |
| A5 | suite 261119-044522-91dbd6 (queued); CI on the push of ee03230 |

Reviewer probes, read 2026-11-19:
- 261118-224440-51acaf (exit 0): seeds 1-10000 x 3000 ok; 78,235,641 apply-index checks pass; 2,536
  seeds re-propose "healed", 19 commit two or more (e.g. seeds 421, 471, 939, 2210, 2888), which the
  review rules harmless.
- 261118-224442-334749 (exit 0): M1 (data voter self-acknowledges ranges ending at multiples of 5)
  KILLED, "node 1 received a range from 6 after applying 2"; M2 (placement term reported one low)
  KILLED by the heal oracle.

F2 (non-blocking, noted, not fixed): the seed 399 regression depends on its trajectory; a change to
the Driver, Core or the simulation's draw order can make it pass without reaching a replaced
placement. The trace rerun 3835e7 shows the retry fires at ee03230. The review's fix (count
`replaced` retries per regression seed and fail on none) is small; left for the lead to order.
F3 (non-blocking): answered in A1 by citing 51acaf. F4: none needed.

## Result

Two findings. Seed 399 was a simulation defect: "healed" placed at 6:126 was replaced by terms 7
and 8, and the heal loop never re-proposed (fix consensus:85ed0e6, seed 399 pinned in
`driver_simulation::run`). The review found the Driver gap the quest names, outside the
simulation's fixed membership: the witness rule read the newest appended membership (fix
consensus:ee03230, `membership_at` the range's end; driver_test `truncated_promotion`).
docs/DRIVER.md states the invariant and its one exception. Trace logging (consensus:40eeec7)
stays. Open: the removal reverse case (Blocked on) and F2.

## Rulings I made

- Lead's message (2026-11-18, push every turn, pre-ruling on a simulation defect): answered by
  following it: branch pushed each turn; A2 is seed 399 pinned in driver_simulation.cc's
  `regressions` with a mutant job, A3 keeps the 10,000 seeds, A4 states the witness rule.
- The regression seed runs inside `driver_simulation::run`, not as a CTest entry: CMakeLists.txt is
  outside the file set (peers-tsan-timeout holds it) and the existing consensus_simulation test
  then covers it.
- The retry rule is "a leader of a higher term than the placement and not healed": the replaced
  index may never commit anything (a shorter new leader's no-op lands below it), so watching the
  committed sequence at that index could wait forever.
- Fix round 1, F1: the lead ordered option 1; the witness test is "witness in the membership at
  the range's last index", exactly as ordered, not "first or last" (see Blocked on, open case).
- The F1 test is tagged A2: it is the deterministic driver_test case for the Driver path A1 now
  names. The mutant is a sed restoring `w.membership()`, which the grep guard checks.
- The sanitizer jobs now run truncated_promotion beside the seed 399 regression, since the A2 test
  is both.
