# consensus-rpc-fetch-lag: journal

State: waiting on jobs 261123-093621-880c1c (A2), 261123-093627-64939a 261123-093627-f49f89 (A1 reruns at consensus:531b655), the suite at 531b655 and CI
Quest: .campaign/detent/quests/consensus-rpc-fetch-lag.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Fix committed and pushed at consensus:2d42387: the fetch case runs a `Lag` sampler from the heartbeat reset on and asserts `beats.worst < 1000 + lag.worst`, printing both (test/transport/rpc_host_test.cc; docs/mechanisms/rpc-lane.md fetch row). Jobs: A1 261123-060820-9e03f1, 261123-060832-50fd33 (25 runs each, 2 at once, 4 burners, strict); A2 261123-060906-b60033 (fails-without at mutant consensus:bebda26, heartbeats sent on Bulk as data, made by .scratch/rpc-fetch-lag/mutant.py from rpc_mutants.py's machinery; check its log fails on the gap, not on a crash); suite 261123-060909-e27d55; CI for A3.

## msg (coordinator: A2 failed)

A1 shards and the suite passed. A2 261123-060906-b60033 FAILED: the Bulk-heartbeat mutant (consensus:bebda26) passed fetch, gaps 150-161 ms. Reading: a heartbeat sent on Bulk is a data frame of its own group, and the Bulk rings serve groups round robin one frame per turn, so it waits for at most one 1 MiB frame, not the backlog; that mutant cannot show head-of-line blocking whatever the bound. The fetch case's Control column in docs/mechanisms/rpc-lane.md is empty (no mutant targets it). Probe: the host's real Control-behind-Bulk mutant `bulk-ahead-of-control` (consensus:cd21eb6, test/transport/mutant_commit.py) run plain against fetch: 261123-090002-e8d6eb. If it fails fetch, A2's target is that mutant, not the Bulk-heartbeat one. 27a1ece merged main (lead); push and `campaign job rerun` the A1 shards at the new head after.

## Round 2 (coordinator: A2 rewritten)

Probe 261123-090002-e8d6eb passed fetch (0 of 4 failed, gaps 105-121 ms): the case cannot show head-of-line blocking. A2 is now a heartbeat stall: mutant consensus:031194e (`fetch-heartbeats-withheld`: the fetch loop sleeps 1.5 s before its fourth beat; .scratch/rpc-fetch-lag/mutant.py), job 261123-093621-880c1c. docs/mechanisms/rpc-lane.md fetch row says the case guards heartbeat stalls, not HOL (consensus:531b655, pushed). A1 shards rerun at 531b655 as above. Check A2's log fails on the gap (about 1500 ms of 1000+lag).

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
