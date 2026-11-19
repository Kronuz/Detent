# driver-apply-gap: journal

State: ready for review
Quest: .campaign/detent/quests/driver-apply-gap.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Code head consensus:9f7bc0c (main 84608cc merged into consensus:8922ed6; pushed). Pending, both
confirmatory: 261118-223705-87f86d (the A1 trace check rerun at the head) and CI run 44473373074
(Kronuz/consensus) at 9f7bc0c. Neither changes the code; if either fails, State goes back to working.

Risk questions for the correctness reviewer:
1. Heal re-proposal safety: the loop proposes "healed" again whenever a leader's term exceeds the
   last placement's term and that placement is not healed everywhere. Can the first "healed" still
   commit after a later one is proposed (two "healed" entries), and does `healed_everywhere` then
   judge the right one? Nothing asserts at most one.
2. Lost entry: the retry keys on term, not on what committed at the placed index. Is there a run
   where the replaced placement's index commits a different entry and the loop keeps re-proposing
   without ever checking it, or where a same-term placement is truncated (it cannot be in Raft, but
   the oracle should not depend on it silently)?
3. Monotonicity: the trace oracle reads `applied(N)` only after a `Committed` range ending at N per
   node. Do term and commit/apply indexes in `on_committed` and the end-of-run check stay monotone
   across the 8 crashes, including a restart that recovers from a checkpoint (`n.applied.clear()`
   in boot)?
4. Simulation reach: before the fix, a heal that never re-proposed failed loudly. After it, the loop
   keeps proposing for 4,000 rounds. Can a real Driver defect that drops or reorders a placement now
   be masked as "replaced, retry"? The mutant 261118-173953-5ceffe shows the oracle bites without the
   retry; nothing shows it still bites for a product bug at the same point.
5. Witness rule (DRIVER.md): the Driver acknowledges a witness's ranges itself, so its apply index
   equals commit with application 0. Is that stated precisely enough that a host cannot read it as
   licence to advance apply without delivery on a data voter?

## Next

1. The lead launches the correctness review; fold its findings by id.
2. If 87f86d or CI 44473373074 fails, read the log and fix inside the file set.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 [manual] | The trace (261118-172655-96dab3 at consensus:40eeec7, `SIM_DRIVER_TRACE=1`, seed 399 x 3000) names the path: no Driver path advances apply past delivery. Node 2 led term 6 (step 2749) and placed 6:120..6:126 while node 3 was down (booted step 2999); the heal loop's "healed" placed 6:126 (heal round 1). Node 3 (founded a Witness for odd seeds, driver_simulation.cc:276) won term 7 (round 37; its vote can only be node 1's, as node 2's log is longer); node 1 won term 8 (round 44); index 120 committed with a new entry (round 46), so 6:120..6:126 were replaced. The old loop re-proposed only on an error or no placement, so it waited 4,000 rounds on 6:126. Nodes 1 and 2: all 101 `applied(N)` lines follow a `Committed` range ending at N on the same node, 0 past delivered; node 3 received no `Committed` range (witness: application 0 by design). Checked by hand (contributor, over the saved trace); rerun at head as 261118-223705-87f86d (asserts the same, plus a second "healed" proposal). |
| A2 | 261118-173937-ee5fda (passed at 85ed0e6a1d7e; only merges from main since); fails without: 261118-173953-5ceffe |
| A3 | TSan 261118-174102-950de0, ASan 261118-174104-2d847a; seeds 1-10000 x 3000 in 20 jobs 261118-174107-e8c4c3 .. 261118-174135-c1443d (all passed at 85ed0e6a1d7e; only merges from main since) |
| A4 [artifact] | `docs/DRIVER.md`, "The apply index never passes what the application was delivered", after "The Sink" (consensus:8922ed6); 261118-223653-0a6417 greps it at 9f7bc0c, exit 0 |
| A5 | suite 261118-174301-6aac26 (8922ed6; only merges since); CI 44438916723 green at 8922ed6, 44473373074 queued at 9f7bc0c |

## Result

The Driver had no apply gap: seed 399 failed on the simulation's heal oracle. "healed" was placed at
6:126 under node 2; terms 7 and 8 replaced that suffix, and the loop never re-proposed because its
call had completed with a placement. Node 3's "application 0" is the witness rule. Fix
(consensus:85ed0e6, test/driver_simulation.cc): re-propose under a leader of a higher term than an
unhealed placement; seed 399 x 3000 runs as a pinned regression inside `driver_simulation::run`.
The SIM_DRIVER_TRACE logging (consensus:40eeec7) stays. docs/DRIVER.md (consensus:8922ed6) states
the apply invariant and the witness exception. The Outcome's "apply index advanced past entries its
application never received" is refuted for this seed; the 10,000-seed sweep at 3,000 steps passes.

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
