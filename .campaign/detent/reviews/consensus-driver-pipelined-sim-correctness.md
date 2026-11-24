# consensus-driver-pipelined-sim review: correctness

Commit: consensus:551a5e6b658574856791915e498104a8f5d53270
Verdict: CHANGES
Date: 2026-11-24

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- 261124-014735-3e572f, -020911-4b0af3, -020914-c49b0b, -020918-cb3518: every oracle holds over 60
  and 120 seeds at the default cycle and at forced depth 4 and 16. Accepted as is.
- 261124-020921-0e0ba5 (`overlap`), -020925-4954e1 (`previous-term`): A2's fails-without mutants.
  Accepted. As the contributor says, both also fail at depth 1 (261124-014439-1733ce, -014442-e66bd8).
- CI 45372452058 at consensus:551a5e6, green on rerun.

## What I checked myself

Scripts are in `.scratch/detent/review-consensus-driver-pipelined-sim-correctness/` in the home
checkout. `probe.sh NAME SEEDS [DEPTH]` patches copies of include/ and test/driver_simulation.cc,
then runs the Driver seeds alone. It exits 0 when the run passes and 1 when an oracle fails.

| Probe | Job | Default cycle, 60 seeds | Forced depth 1, 60 seeds |
|---|---|---|---|
| `ack-tip`: a success acknowledges through the pipeline's last flight, not the answered one (core.h:1318) | 261124-045554-49db14 / -045557-2380a0 | killed, seed 21 (liveness after healing: a follower stuck at 19 of 24) | survives (equivalent at depth 1) |
| `reject-keep`: a rejection drops only its own flight; later flights with the failed prefix stay out (core.h:1339) | -045601-5793f3 / -045604-9b0998 | survives | survives |
| `reject-tip`: as above, and `next` is not rolled back to the rejected flight's start (core.h:1323) | -045608-bf0238 / -045615-003c7b | survives | survives |
| `hold-term+more-term`: both term checks on merged appends removed (worker.h:609, core.h:1098) | -045625-1bbcdd | survives | not run |
| `coverage`: counters added beside the quest's own | 261124-043742-a8411c | see F1 and F2 | |

Coverage, 60 default seeds (261124-043742-a8411c):

1. Of 15 depth-1 seeds, 11 are flagged "a second append in flight".
2. 19 of 46 deeper seeds had a third append in flight.
3. 179 successes arrived with a later flight still out.
4. 3 rejections arrived with a later flight still out.
5. 6 appends with entries arrived below a term their follower had already seen.

Timing: strict jobs, CI's default (20 seeds, 3000 steps), `/usr/bin/time -p`. Head at
`SIM_DRIVER_PIPELINE=1` prints counters identical to main's, so it runs the same trajectory.

| Arm | Run 1 | Run 2 |
|---|---|---|
| main 54c6599 | 23.23 s (261124-041416-2ad19a) | 25.88 s (-041419-9eb962) |
| head at depth 1 | 26.36 s (-041346-614ef3) | 21.66 s (-041357-bd6974) |
| head at the default cycle | 29.70 s (-041351-91fefd) | 27.36 s (-041401-f02d13) |

The counters cost nothing measurable (24.0 s against 24.6 s mean). The default cycle is about 16%
slower. That is the work of the pipelined seeds themselves: 18877 appends against 17615, plus
follower batching. CI's linux-asan-ubsan `consensus_simulation` took 107.96 s at head. Runs at
sibling commits without this change took 57.94 s (45373952149) and 105.64 s (45370136336).
Every run is well under the test's 300 s TIMEOUT.

Interface for driver-pipeline (its A2 asks for 2-4 loops per host at depth 1, 4 and 16 over
10,000 seeds):

1. `seeds steps first` and `SIM_DRIVER_SEEDS` are unchanged.
2. `SIM_DRIVER_PIPELINE=N` fixes the depth.
3. Hosts boot with 2-4 loops.

All three are kept.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | gap | A1's counter does not tell pipelining from a resend. It flags 11 of 15 depth-1 seeds as "a second append in flight", which a depth-1 leader cannot have (core.h:1493). At depth 1 it also counts 32 of 17615 appends as pipelined, at most 2 in flight. So "45 of 46 seeds" does not prove "more than one append in flight on most seeds". Real pipelining does run: at most 4 in flight, a third in flight on 19 of 46 deeper seeds, 179 successes with a later flight out, and `ack-tip` is killed. But the per-seed figure A1 rests on is unproven. The cause is not traced. Likely a resend inside the counter's 20-tick window: flights reset by timeout on a fast-clock node (`n.rate`, test/driver_simulation.cc:613) or by a snapshot. | 261124-043742-a8411c; -041346-614ef3 (depth 1: "32 of them with another in flight ... at most 2"); test/driver_simulation.cc:394-401 | Count an append as pipelined only when it extends an outstanding flight of the same term: its `previous` is at or past that flight's end. A resend starts at or below it. Show the forced-depth-1 run printing 0 pipelined appends and 0 flagged seeds, then rerun A1's 60 seeds and state the per-seed figure the corrected counter gives. |
| F2 | no | gap | A rejection with later flights out, and a leader change with appends in flight, are each reached only a few times per 60 seeds: 3 and 6 events. Two pipelining-only mutants of the rejection path survive at every depth: `reject-keep` and `reject-tip`. Both only waste flights, which heal by timeout or by the next rejection, so no oracle can see them. They are benign to safety, not a hole the oracle should close. A deterministic case in test/pipeline_test.cc (verified peer at depth 4, the oldest flight rejected, assert one flight at the hint) would pin the reset. | 261124-045601-5793f3, -045608-bf0238, -043742-a8411c | Optional: that pipeline_test case, outside this quest's file set. |
| F3 | no | nit | The five surviving batching mutants are equivalent. Each Worker check has a Core counterpart. Contiguity is re-checked by the previous-entry check (core.h:1210). Term is re-checked by the merged loop's `more.term != hard_.term` (core.h:1098). Peer follows from term, since one leader holds a term. Bounds are re-checked by `receive_usage` (retained_state.h:73), which drops an oversized merge whole. Removing both term checks still survives 60 seeds. That case is reachable only when a term change lands inside a held batch (6 stale-term deliveries in 60 seeds), so the survival is not evidence either way. | 261124-045625-1bbcdd; worker.h:603-616; core.h:1097-1100 | None needed. docs/mechanisms/simulation.md already says a single-check mutant is unobservable. |
| F4 | no | nit | The +16% runtime is inherent to the pipelined seeds. The counters themselves cost nothing measurable, and the ASan time stays within CI's limit. | timing table above; CI 45372452058 job 138411462886 (107.96 s, TIMEOUT 300) | None. |
| F5 | no | nit | `SIM_DRIVER_PIPELINE=N` couples depth with follower batching (on above 1) and with no window or frame cap. driver-pipeline cannot run depth 4 without batching, or under a binding window, at a fixed depth. | test/driver_simulation.cc:118-122 | When driver-pipeline needs it, a separate `SIM_DRIVER_BATCHING=0/1`. |
| F6 | no | nit | docs/mechanisms/simulation.md is outside the quest's file set (`campaign brief` warns). The edit is a docs-only description of the change. | `campaign brief consensus-driver-pipelined-sim` | Lead: widen the file set, or accept. |

## Not run

- TSan, ASan or any sanitizer myself. I relied on CI 45372452058.
- The core (non-Driver) simulation. It runs only at Flow{} depth 1 (test/simulation_test.cc has no
  Flow), so the Driver simulation is the only pipelined simulation.
- Whether `hold-term+more-term` is ever reached, and the exact cause of F1's false positives
  (neither was traced).
- Seeds beyond 60 for the mutants, and test/pipeline_test.cc against `ack-tip`.
