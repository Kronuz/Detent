# consensus-rpc-fetch-lag: journal

State: waiting on jobs 261123-060820-9e03f1 261123-060832-50fd33 261123-060906-b60033 261123-060909-e27d55 and CI at consensus:2d42387
Quest: .campaign/detent/quests/consensus-rpc-fetch-lag.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Fix committed and pushed at consensus:2d42387: the fetch case runs a `Lag` sampler from the heartbeat reset on and asserts `beats.worst < 1000 + lag.worst`, printing both (test/transport/rpc_host_test.cc; docs/mechanisms/rpc-lane.md fetch row). Jobs: A1 261123-060820-9e03f1, 261123-060832-50fd33 (25 runs each, 2 at once, 4 burners, strict); A2 261123-060906-b60033 (fails-without at mutant consensus:bebda26, heartbeats sent on Bulk as data, made by .scratch/rpc-fetch-lag/mutant.py from rpc_mutants.py's machinery; check its log fails on the gap, not on a crash); suite 261123-060909-e27d55; CI for A3.

## Next

1. On jobs: read the A2 log for the reason, paste `campaign brief`'s table, write the Result, set ready for review.

## Blocked on

Nothing.

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

<Decisions inside the quest's scope, so the reviewer can check them.>
