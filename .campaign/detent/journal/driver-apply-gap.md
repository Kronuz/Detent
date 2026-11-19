# driver-apply-gap: journal

State: waiting on jobs 261118-172655-96dab3 261118-173937-ee5fda 261118-173953-5ceffe 261118-174301-6aac26 and the A3 chain
Quest: .campaign/detent/quests/driver-apply-gap.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Waiting on the seed 399 trace (261118-172655-96dab3, at consensus:40eeec7, `SIM_DRIVER_TRACE=1`;
read with `campaign job log -n 400 <id>`), which decides between the lead's two branches (msg from
the lead, 2026-11-18: a simulation defect, or a real apply gap in driver.h).

Hypothesis, not yet confirmed by the trace: the defect is in the simulation's heal loop. Seed 399
is odd, so node 3 is founded a witness: it is delivered nothing, so "application 0" is expected,
and `healed_everywhere` already skips it. Nodes 1 and 2 show application 120 = applied 120. The heal
loop proposed "healed" again only when its last call failed or was not placed; a placement a later
term truncated never commits, so the loop runs out its 4,000 rounds.

Candidate fix committed ahead of the trace, so its jobs queue now: consensus:85ed0e6 re-proposes
under a leader of a higher term than the placement, and pins seed 399 x 3,000 steps as a regression
that `driver_simulation::run` runs beside every range (so `SIM_DRIVER_SEEDS=0 ... 1 1 1` runs it
alone, and CTest's consensus_simulation runs it). consensus:8922ed6 adds the apply invariant and the
witness exception to docs/DRIVER.md ("The apply index never passes what the application was
delivered", after "The Sink").

Jobs (all at consensus:85ed0e6 unless noted):
- A2 261118-173937-ee5fda: seed 399 regression passes with the fix.
- A2 261118-173953-5ceffe (--fails-without, --fresh): the mutant drops `|| replaced`, seed 399 must
  fail on the heal oracle; a build failure or an unapplied sed exits 0.
- A3, chained by --after from the A2 job (D120): TSan 261118-174102-950de0, ASan
  261118-174104-2d847a, then seeds 1-10000 x 3000 in 20 jobs of 500: 261118-174107-e8c4c3 ..
  261118-174135-c1443d (`campaign job list | grep driver-apply-gap`).
- A5 suite 261118-174301-6aac26 at consensus:8922ed6; CI run 44438916723 (Kronuz/consensus) at 8922ed6.

## Next

1. Read the trace. Confirm: the last healed proposal's placement term < the final leader's term 8,
   and nothing healed committed at its index; cite the lines. Prove the product right in that run
   (lead's item 3): nodes 1 and 2 applied(N) only after a Committed range ending at N; node 3 has
   no Committed line (witness). Then reword the outcome below under Result.
2. If the trace refutes it: revert 85ed0e6's heal change (keep the trace and regression seed),
   trace the apply path the trace names, and the quest stands as written.
3. The A2/A3 jobs ran at 85ed0e6, the docs commit 8922ed6 is code-identical; if `campaign brief`
   wants them at the head, `campaign job rerun`. (Cancelling the stale queue was refused by the
   permission classifier; nothing to cancel if they are reused.)

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| <A1> | <from `campaign brief`> |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

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
