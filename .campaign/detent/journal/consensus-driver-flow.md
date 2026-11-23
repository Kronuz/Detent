# consensus-driver-flow: journal

State: waiting on jobs 261123-063949-57aa77 261123-063953-58e218 261123-063957-782827
Quest: .campaign/detent/quests/consensus-driver-flow.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Committed and pushed consensus:10eb12e (WorkerLimits::flow to all four Core builds; docs; driver_test flow_reaches_core, passes locally 830 checks). A1 job 261123-063949-57aa77; A2 mutants (local branches mutant/driver-flow-create 46e0f17, -recover 80c7c8e) jobs 261123-063953-58e218, 261123-063957-782827; CI on the pushed branch is A4.

## Next

1. When the three jobs finish: paste `campaign brief` Evidence; check CI (A4).
2. Fill Result; State ready for review.

## Blocked on

<What, and who decides. "Nothing" when nothing.>

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), name every job you examined, failures included (`campaign brief` lists failed
jobs no journal names), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | job 261123-063949-57aa77 (queued): driver_test, all cases, at consensus:10eb12e; local run of flow_reaches_core 830 checks, 0 failures |
| A2 | jobs 261123-063953-58e218 (create drops the Flow, mutant/driver-flow-create 46e0f17) and 261123-063957-782827 (recover drops it, mutant/driver-flow-recover 80c7c8e), --fails-without (queued) |
| A3 | [artifact] docs/DRIVER.md, 'Replication flow (D15)' above the Seams block: names `WorkerLimits::flow` / `GroupOptions::scheduling.flow`, default `Flow{}`; CHANGELOG.md 2026-11-23 |
| A4 | CI on the pushed branch quest/detent/consensus-driver-flow at consensus:10eb12e (running) |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

- The option is `WorkerLimits::flow` (default Flow{}), not a new Worker constructor parameter or a second GroupOptions field: Worker takes its Limits/Timing/scheduling positionally (inserting a parameter breaks callers outside this file set), and GroupOptions already passes `scheduling` whole to Worker, so one field serves both (`GroupOptions::scheduling.flow`). Strata sets `o.scheduling.flow = Flow{4, 4 << 20, 256 << 10}`.
