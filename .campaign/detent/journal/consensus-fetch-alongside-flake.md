# consensus-fetch-alongside-flake: journal

State: working
Quest: .campaign/detent/quests/consensus-fetch-alongside-flake.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Diagnosis (A1). consensus:3965055 instruments the case: each inexact fetch's error or first
differing byte, each commit past 2 s, the term change, per-node links/gone/aborted calls, the
host's worst stall. Repro jobs 261123-124608-877991 (30 runs serial, 4 burners) and
261123-124615-cd5278 (24 runs, 3 at once, 4 burners) run repro.sh
(~/.scratch/detent/consensus-fetch-alongside-flake/repro.sh) at it.

CI facts so far: both failures (runs 44996244171 at main a068ba06; 45240308075 attempt 1) are
the same shape: 13 of 16 exact, 7 commits, slowest 7,077 / 4,410 ms, and node 1 lost term 1
(an election happened). The install case ran beside it both times (ctest --parallel 2).

## Next

1. Read the repro jobs' misses; name the cause (error kind vs bytes).

## Blocked on

Nothing

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
