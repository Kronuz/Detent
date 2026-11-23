# consensus-simulation-asan-timeout: journal

State: waiting on jobs 261123-120158-c6e7a8 261123-120851-a1e636 261123-120853-0b47e0 261123-120854-b69029 261123-120855-c2ed8d 261123-120857-272231 261123-120859-a03833 261123-120900-0d6648
Quest: .campaign/detent/quests/consensus-simulation-asan-timeout.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Jobs queued (scripts in consensus worktree .scratch/asan-timeout/):
- A1 261123-120158-c6e7a8: strict A/B main vs fix (asan-ab.sh): driver seed 16 alone, 17, default.
- A2 261123-120851-a1e636: default run at the fix under 150 s (half the TIMEOUT) on this
  host, which ran main at 373 s vs CI's typical 170 s (>= 2x slower than CI typical).
- A2 --fails-without 261123-120853-0b47e0: the same bound at main 531b655.
- A3 261123-120854-b69029 261123-120855-c2ed8d 261123-120857-272231 261123-120859-a03833
  261123-120900-0d6648: 5 shards x 10 runs (5 parallel x 2), limit 200 s.
- Pushed quest branch at consensus:b5e1d47 for ci:linux-asan-ubsan, ci:ci (A4).

## Findings

- Not a hang. The failing CI logs show no progress line (stdout fully buffered; the test
  printed nothing before the kill). 46 ASan runs on 2026-11-21..23 fall into runner classes:
  both timeouts ran on the slow class (consensus_consensus 4.41/4.69 s vs 2.8-3.1 s typical,
  liveness 169/176 s vs ~80 s, shared_worker 233/258 vs ~140), every test 1.5-2.2x slower.
- The simulation is deterministic: per-seed heal round counts repeat exactly across runs.
- It tripled (58 -> 170 s typical CI) at consensus:8780e44 (Driver install). Local probe at
  main (consensus .scratch/asan-timeout/probe-main.txt): driver seeds 10, 15, 16, 18 each
  idle 9,800-13,900 heal rounds (63+47+83+38 = 231 s of a 373 s run) waiting out the 60 s
  snapshot_timeout of a stalled image transfer; the sim's other timers are scaled 5-10x down.
- Core sim heal: always 4,000 rounds, proposing only from round 2,000; 53% of the core phase
  (macOS `sample`). With proposals from round 0 and a stop once a majority applied: healed
  within 4-713 rounds (median 130) on seeds 1-200.

## Fix (consensus:04bb069, doc consensus:b5e1d47)

- driver_simulation.cc: snapshot_timeout 60000 -> 1000 ms (5x election_max).
- simulation_test.cc: core heal proposes every 50 rounds from round 0 and stops once a
  majority applies; stdout line-buffered.
- docs/mechanisms/simulation.md.
- Local probes (not evidence): full run 387 s -> 208 s under the same load (snapshot_timeout
  alone); core phase 47 -> 32 s; sweep of driver seeds 21-120 and core seeds 201-400 at the
  fix passes.

## Next

1. A2: bound job at the fix and --fails-without at main.
2. A3: 5 strict shards of 10 runs (asan-many.sh), none over 200 s.
3. Push; cite ci:linux-asan-ubsan, ci:ci with the simulation's CI time.

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
