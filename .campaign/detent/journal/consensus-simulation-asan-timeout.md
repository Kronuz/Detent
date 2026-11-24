# consensus-simulation-asan-timeout: journal

State: waiting on jobs 261123-164555-fab10d 261123-164556-ea8620 261123-164611-b61059 261123-164613-bc5203 261123-164638-102d1c 261123-164640-63086e 261123-164642-b0667c 261123-164644-7f8b53 261123-164646-e01d38 261123-164648-db8cb5 261123-164651-246bb9 261123-164655-774a2b 261123-164659-f34d60
Quest: .campaign/detent/quests/consensus-simulation-asan-timeout.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Round 2 (session 2), at consensus:cbe0522 (main 3aa1355 merged in, clean). Scripts in the
consensus worktree's .scratch/asan-timeout/; asan-time.sh now reports wall and CPU seconds and,
with CAP=1, kills the run at its limit.
- A1 one arm per job: main 261123-164611-b61059, fix 261123-164613-bc5203 (driver seed 16 alone,
  seed 17 alone; asan-seeds.sh).
- A2 at the fix 261123-164555-fab10d (under 150 s); --fails-without at main 261123-164556-ea8620
  (CAP=1: killed at 150 s, so it never waits main's ~370 s out).
- A3 9 shards x 6 runs (3 at once x 2 rounds), limit 200 s: 261123-164638-102d1c
  261123-164640-63086e 261123-164642-b0667c 261123-164644-7f8b53 261123-164646-e01d38
  261123-164648-db8cb5 261123-164651-246bb9 261123-164655-774a2b 261123-164659-f34d60.
- A4: `campaign verify --for A4` queued; branch pushed at cbe0522 for ci:linux-asan-ubsan, ci:ci.

Round 1 jobs (at b5e1d47, now other code): A1 261123-120158-c6e7a8 and A2 --fails-without
261123-120853-0b47e0 hit the 10-minute cap (main's ASan run beside light holds); A2
261123-120851-a1e636 passed (123.6 s); A3 shards b69029 c2ed8d 272231 a03833 passed (max 168.7 s),
0d6648 failed (round 1: five copies 247.3-250.3 s, round 2: 165.7-168.7 s). c6e7a8's finished
lines: seed 16 alone main 195.8 s vs fix 27.5 s; seed 17 main 18.6 s vs fix 31.1 s.

## Findings

- Shard times are not comparable to CI's 170 s: CI is about 2.2x faster than this host. Same
  runner class, CI ASan job: main consensus:df4f8f8 171.45 s (run 45204437379) vs fix b5e1d47
  57.38 s (run 45256348228, job 137979080621), neighbours equal (liveness 81.46 vs 81.66 s):
  the fix cuts CI time 3.0x, to 19% of the 300 s TIMEOUT, and back to the 58 s before 8780e44.
  Locally one run alone is 123.6 s (a1e636) vs main's 373 s (probe-main.txt).
- Shards ran 5 ASan copies at once; on macOS a strict hold shares the cores with light holds
  (quiet.py: Macs can't split cores), so five copies plus 6-15 light holds stretch 124 s to
  148-169 s. The 250 s round is load, not a seed: the default run is deterministic (same seeds in
  every copy), all five copies of that round took 247-250 s and the same binary's next round
  165.7-168.7 s. Round 2 shards record CPU seconds to show it: flat CPU, moving wall.
- What remains at the fix (full-1000.txt trace): 20 driver seeds of ~3 s run + 0.3-3.5 s heal
  each, no idle stretches; the rest is the core seeds' steps. No single cost dominates.

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
