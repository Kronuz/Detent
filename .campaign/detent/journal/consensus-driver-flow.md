# consensus-driver-flow: journal

State: working
Quest: .campaign/detent/quests/consensus-driver-flow.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Header change (WorkerLimits::flow reaches all four Core builds) and docs written in consensus worktree (uncommitted until the local driver_test flow_reaches_core passes); test in test/driver_test.cc.

## Next

1. Local build/run of driver_test flow_reaches_core (.scratch/driver-flow/build).
2. Commit; A1 job; mutant commit (worker.h create/recover drop flow) for A2 --fails-without; push for CI (A4).

## Blocked on

<What, and who decides. "Nothing" when nothing.>

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), name every job you examined, failures included (`campaign brief` lists failed
jobs no journal names), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| <A1> | <from `campaign brief`> |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

- The option is `WorkerLimits::flow` (default Flow{}), not a new Worker constructor parameter or a second GroupOptions field: Worker takes its Limits/Timing/scheduling positionally (inserting a parameter breaks callers outside this file set), and GroupOptions already passes `scheduling` whole to Worker, so one field serves both (`GroupOptions::scheduling.flow`). Strata sets `o.scheduling.flow = Flow{4, 4 << 20, 256 << 10}`.
