# driver-apply-gap: journal

State: waiting on jobs 261118-172655-96dab3
Quest: .campaign/detent/quests/driver-apply-gap.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

A1, tracing seed 399. consensus:40eeec7 adds `SIM_DRIVER_TRACE=1` to driver_simulation.cc: it logs
Committed ranges, applied() acks, proposals, roles, crashes, boots, transfers and cancels per step
(heal rounds are steps 1000000+), and a failed heal prints its last proposal and what committed at
its index. Trace job 261118-172655-96dab3 (no --for: A1 is [manual]); read it with
`campaign job log -n 400 261118-172655-96dab3`.

Lead hypothesis to check: seed 399 is odd, so `Simulation(seed, seed % 2 == 1)` founds node 3 as a
witness. A witness never gets Committed, so "node 3 application 0" is expected, not an apply gap,
and `healed_everywhere` skips it. Nodes 1 and 2 have application 120 = applied 120. So the failure
is more likely the heal loop itself: it re-proposes only when the last "healed" call errors or has
no placement; a placement superseded by a later term (a leftover transfer's TimeoutNow, or an
election in heal) is never re-proposed, so the loop spins out its 4,000 rounds.

## Next

1. Read the trace: confirm or refute (placed term:index of the last healed proposal vs what
   committed there; any election during heal rounds).
2. If confirmed, the defect is in the simulation oracle, not driver.h: fix heal to re-propose when
   a leader of a higher term than the placement exists and the entry is not healed; A2 then needs a
   ruling (a driver_test case for an oracle bug cannot fail without the fix): bring to the lead.
3. If refuted, trace the apply path named by the trace and continue A2.

## Blocked on

<What, and who decides. "Nothing" when nothing.>

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

<Decisions inside the quest's scope, so the reviewer can check them.>
