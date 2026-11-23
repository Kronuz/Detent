# consensus-simulation-asan-timeout: journal

State: working
Quest: .campaign/detent/quests/consensus-simulation-asan-timeout.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Local ASan build (consensus worktree .scratch/asan-timeout/build) to time the driver
simulation's healing and repair waits per seed.

## Findings so far

- The failing CI logs show no progress line: the test prints only at its end, so a timeout
  leaves nothing (ctest --output-on-failure printed an empty body).
- Not a hang: 46 ASan runs (2026-11-21..23) fall into runner classes by consensus_consensus
  time. Both timeouts (45234099713, 45204597820) ran on the slow class: consensus_consensus
  4.41/4.69 s vs 2.87 s, liveness 169/176 s vs 81 s, total 2512/3105 s vs 1583 s on the passing
  run of the same commit (45204437379). Every test on those runners is 1.5-2.2x slower.
- The simulation tripled at consensus:8780e44 (driver install merged): 58 s -> 170 s on the
  normal class. 4d5b6e2 made the repair check wait 1000 + 30000 quiet rounds (2x the 60 s
  snapshot_timeout at 4 ms/round); healing waits 4000 + 30000. Hypothesis: those simulated idle
  rounds dominate.

## Next

1. Time driver seeds alone vs core seeds alone under ASan; count quiet rounds per seed.
2. Bound the waits (shorter snapshot_timeout in the sim's Timing, or skip idle rounds).
3. A2/A3 jobs; push for A4.

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
