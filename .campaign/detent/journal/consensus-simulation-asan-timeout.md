# consensus-simulation-asan-timeout: journal

State: waiting on jobs 261123-231348-ff18c4 261123-231353-be4231
Quest: .campaign/detent/quests/consensus-simulation-asan-timeout.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Review: correctness APPROVE at consensus:2db1a9d (Detent:84f19b6e). Lead ruling D152 on F1:
bound the core heal at 2,000 rounds. Done at consensus:eb9153f (test/simulation_test.cc:240,
docs/mechanisms/simulation.md); main c177c88 merged in at consensus:54c6599 and pushed for CI.
- Confirming default ASan run at the fix: 261123-231348-ff18c4 (A2, limit 150 s).
- Heal probe: 261123-231353-be4231, the reviewer's 747c2a core-heal probe with inst.py's new loop
  pinned at `round < 2000` (heal-probe/ in .scratch/asan-timeout); it fails unless the source's
  bound is 2,000, and prints each loop's max majority round for seeds 1-200.

Round 2, at consensus:cbe0522 (main 3aa1355 merged in, clean). Scripts in the consensus
worktree's .scratch/asan-timeout/; asan-time.sh reports wall and CPU seconds and, with CAP=1,
kills the run at its limit. All 13 jobs passed:
- A1 one arm per job (asan-seeds.sh): main 261123-164611-b61059 seed 16 alone 67.8 s, seed 17
  5.4 s; fix 261123-164613-bc5203 seed 16 9.1 s, seed 17 7.5 s.
- A2 fix 261123-164555-fab10d 90.9 s (limit 150 s); --fails-without at main 261123-164556-ea8620
  killed at 150.1 s.
- A3 9 shards x 6 runs (3 at once x 2 rounds), limit 200 s, 54 runs, wall 76.1-188.7 s:
  102d1c 110.8-123.4, 63086e 98.7-114.3, b0667c 99.5-126.7, 7f8b53 101.7-130.0, e01d38 76.1-78.6,
  db8cb5 131.3-156.1, 246bb9 141.7-188.7, 774a2b 150.3-184.0, f34d60 127.3-166.2 (ids 261123-1646..).
- CI 45299975989 at cbe0522: attempt 1 failed outside this diff, attempt 2 green (A4).
  1. linux-asan-ubsan: consensus_liveness Timeout 180.14 s (filed by the lead as quest
     consensus-liveness-asan-timeout). consensus_simulation passed in that attempt in 112.52 s,
     a slow-class runner.
  2. macos-x86_64: consensus_transport_groups_fetch-alongside-consensus "Subprocess aborted"
     after 10.15 s, the flake quest consensus-fetch-alongside-flake owns.
  Attempt 2: consensus_simulation 59.09 s, 145 of 145 ASan tests passed.

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
  165.7-168.7 s. CPU seconds do not separate load from work on this Mac: round 2's runs took
  75.6-161.7 s of CPU for the same work, tracking wall (contended runs land on slower cores and
  clocks), so the evidence is the deterministic run plus the uniform slow round.
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

1. Review (correctness lens); fold in findings.

## Blocked on

Nothing

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), name every job you examined, failures included (`campaign brief` lists failed
jobs no journal names), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | fix arm 261123-234627-ba4f71 at consensus:54c6599 (rerun of 261123-164613-bc5203: seed 16 alone 9.1 s); main arm 261123-164611-b61059 at main (67.8 s) |
| A2 | 261123-164555-fab10d; fails without: 261123-164556-ea8620 |
| A3 | 261123-234628-4a9e7f, 261123-234629-04ee62, 261123-234631-e6bbf2, 261123-234633-c71016, 261123-234635-528637, 261123-234638-5eef1a, 261123-234639-fc1877, 261123-234641-c0a1e9, 261123-234642-9d6dc5 (9 shards of 6 ASan runs at consensus:54c6599, none over 200 s; reruns of the 164638-164659 round) |
| A4 | `ci:linux-asan-ubsan`, `ci:ci`: run 45299975989 attempt 2 green at consensus:cbe0522 (consensus_simulation 59.09 s, 145/145 ASan tests); attempt 1's two failures are outside this diff (Doing, CI) |

Jobs examined and superseded: round 1 261123-120158-c6e7a8, 261123-120853-0b47e0 (10-minute
cap), 261123-120851-a1e636, 261123-120854-b69029, 261123-120855-c2ed8d, 261123-120857-272231,
261123-120859-a03833 (passed, other code), 261123-120900-0d6648 (failed: load, see Findings);
261123-164551-a3133b, 261123-164552-5cd94e (cancelled: same command superseded, resubmitted).

## Result

The simulation's ASan time was set by idle healing, not a hang. Two bounds fix it (consensus:04bb069):
the Driver simulation's `snapshot_timeout` scales with its other timers (60,000 -> 1,000 ms), and
the core simulation's heal proposes every 50 rounds from round 0 and stops once a majority applies.
- CI linux-asan-ubsan, same (typical) runner class: 171.45 s (main df4f8f8, run 45204437379) ->
  57.38 s (b5e1d47, run 45256348228); 59.09 s at cbe0522 (45299975989 attempt 2), 112.52 s on a
  slow-class runner (attempt 1): under half the 300 s TIMEOUT on a 2x slower host.
- Local ASan, one run: 373 s (main, probe-main.txt) -> 123.6 s (a1e636), 90.9 s (fab10d); main
  killed at 150 s (ea8620). 54 runs at the fix, 76.1-188.7 s, none over 200 s (A3).
- The stalled-transfer seed: driver seed 16 alone 67.8 s -> 9.1 s (b61059, bc5203). Seed 17,
  which holds no stalled transfer, 5.4 -> 7.5 s: with a 1 s timeout a slow transfer restarts
  inside the run (9 SnapshotNeeded vs 5, 40 frames vs 32), more coverage of the restart path.
- Docs: docs/mechanisms/simulation.md (runner classes, where the time goes, the numbers).

Oracles. The diff to the tests is 10 lines (git diff 531b655 HEAD -- test/simulation_test.cc
test/driver_simulation.cc): one constant, the heal loop's condition and proposal cadence, and a
setvbuf. No check was added, removed or edited: `fail(`/`expect(` sites are 82 in
simulation_test.cc and 31 in driver_simulation.cc at both main 3aa1355 and the fix.
Core simulation (simulation_test.cc), checked on every action of every round, heal included:
1. One leader per term (two leaders in term).
2. Leader completeness (leader lacks applied entry).
3. State machine safety (two different entries applied at an index).
4. Commit only on a quorum (committed index held by too few).
5. Single-step membership changes; membership matches the durable log.
6. One vote per term; votes and acknowledgements only after they are durable.
7. Applied sequences are prefixes of one another; log matching (check_logs, every 16 steps and
   at the end of each phase, after healing included).
8. One storage and one application batch outstanding; no fencing; reads see every earlier commit.
9. Restart: reopen keeps its term, closed records round-trip.
10. Liveness: the healed group commits a new entry on a majority within 4,000 rounds.
11. The closing and quiescence phases' expectations (unchanged code, separate phases).
Driver simulation (driver_simulation.cc): state machine safety per group, one leader per group
and term, images validated before activation and equal to the committed sequence, no witness
receives ranges or images, repairs never below the checkpoint, every call completes exactly once
with a value or an error, Placement/committed()/read semantics, loops stop and groups close
cleanly, no repair pending after healing, and liveness: every member applies a new entry after
healing.

Does stopping the core heal once a majority applies weaken them? No safety oracle is weakened:
each runs on every action, so the heal now feeds them fewer post-heal quiet rounds, not weaker
checks, and check_logs() still runs after the loop. The liveness oracle is the same condition
(`applied_healed() < healed_quorum()`) with the same budget from the first proposal: main
proposed from round 2,000 of 4,000, so a majority had 2,000 rounds after it; the fix proposes
from round 0 and bounds the loop at 2,000 (D152, consensus:eb9153f). Seeds 1-200 reach the
majority within 713 rounds (2.8x headroom):

```cpp
for (int round = 0; round < 2000 && !failures && applied_healed() < healed_quorum(); ++round) {
	quiet_round();
	if (round % 50 == 0) { /* every up leader proposes "healed" */ }
}
if (applied_healed() < healed_quorum()) { fail("healed group did not commit a new entry on a majority"); ... }
check_logs();
```

The old loop's extra rounds after the majority applied were faultless quiet rounds on a healed
group, where no oracle fires at main on the default seeds 1-200; the fault phase (10,000
steps a seed) is untouched. What it gives up is the minority's catch-up after the majority,
which the old loop never asserted either (it checked a majority, not all). The Driver heal
already stopped once every group healed; its bound is 4,000 + 2 x snapshot_timeout / 4 rounds,
so it shrinks with the timeout (34,000 -> 4,500) and still outlasts two transfer restarts on the
slowest node's clock, and its liveness oracle (every member applies) is unchanged.

Leaves open: consensus_liveness's own ASan timeout (quest consensus-liveness-asan-timeout).

## Rulings I made

- snapshot_timeout 1,000 ms (5x election_max; the core simulation's is 2.5x), not the
  production 60 s, which no quiet heal can afford.
- Core heal proposes from round 0, every 50 rounds, and ends at the majority it asserts, within
  2,000 rounds (the lead's D152).
- A3 ran as 9 strict shards of 3 concurrent copies x 2 rounds to fit the 10-minute cap.
