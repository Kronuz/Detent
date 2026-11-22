# consensus-quiesce-election-stall review: correctness

Commit: consensus:dbb22e7e608c2179053ed304160e4be40b0f56ca
Verdict: CHANGES
Date: 2026-11-21

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- 261121-141823-59e17c (seed 1080 at rest elects), 261121-141825-00c318 / 261121-141828-d3de36 /
  261121-141831-ab6b36 (seeds 1-3000 at rest pass in Release, ASan+UBSan and TSan), all at
  consensus:c45bb24. dbb22e7 only adds ctest entries (CMakeLists.txt), so they stand for A1's sweep.
- 261121-141834-bf5253: TSan ctest at c45bb24, simulation (including `quiescence::run`), driver,
  driver_close*, liveness and the plain replace parts: 10 of 10 pass. This settles question 3's
  idle case: `quiescence::at_rest` sends nothing for 80 election timeouts, and driver_close's
  idle mode reaches zero armed timers.
- 261121-141023-81b8cd (pre-fix de24723) and 261121-141837-c1601b (mutant at c45bb24): seed 1080
  fails "no leader after settling".

## What I checked myself

Scripts under `.scratch/detent/review-consensus-quiesce-election-stall-correctness/` in the home.

- `wake.sh probe_wake.cc`: Core-level orderings with three voters at rest and a removed member's
  refused pre-vote (node 9), at the commit and with `wake-ack-unanswered`. Jobs 261121-194220-31777a
  (s1-s5) and 261121-211443-0a9c3e (s1-s6).
  - s6, the exact 1080 ordering (a follower's acknowledgement on waking reaches a leader still
    awake for the other follower's answer): the fix keeps the leader awake and every replica
    rests again, with no term change. The mutant leaves the leader at rest with that follower's
    timer running. The rule works, and the mutant fails on it.
  - s2, the busy ordering: the commit heartbeat that rests both followers is still persisting
    when the pre-vote wakes them. Both builds give the same result: the leader rests with both
    followers' timers running, and term 1 becomes term 2 with the leader deposed.
  - s4 (acknowledgement overtakes the answer to the leader's round) and s5 (both acknowledgements
    lost): same as s2 on both builds; the leader is deposed.
  - s2c (the pre-vote after the persistence) and s1: no term change on either build.
  - s3 (every message delivered twice): the leader never rests while duplication lasts, on both
    builds. It rests within two heartbeats once duplication stops.
- `exposure.sh DIR 3000`: driver_replace_test, instrumented by `exposure_patch.py`, counts each
  span while settling in which the leader rests and a follower of its term (not the leaving
  member) is awake for at least election_min. Such a span means that follower's election timer
  fired against a live leader at rest. Job 261121-194253-77ca8b:
  - fix, quiesce: 222 such spans in 3000 seeds (longest 188 ms), all 3000 seeds pass;
  - mutant, quiesce: 200 spans in the 1079 seeds before 1080 stops it;
  - fix, plain: 0 spans;
  - seeds that elect while settling: 609 at rest and 617 plain. Most spans heal without an
    election: the follower's pre-vote wakes the leader, and the other follower refuses it.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | defect | Question 2: the rule closes only the 1080 ordering, not the class. A follower woken while it persists (core.h:563 `if (leader_ && !busy())`) sends no acknowledgement, and a busy core ignores the frame (core.h:1006). Its deferred answer matches the leader's flight, so `told` is set (core.h:1218), and the leader rests with the follower's timer running. Every follower persists on the commit heartbeat that rests it, because `commit_index` is hard state, so this window opens on every rest. With both followers caught in it, the live leader is deposed. Over the sweep, 222 spans at the fix against 200 in 1079 mutant seeds: the fix removes some exposure, not all of it. | probe s2, job 261121-211443-0a9c3e; job 261121-194253-77ca8b | One follower-side rule covers F1 and F3: a woken follower re-acknowledges its log every heartbeat interval until a frame from its leader rests it again. Its timer already runs, so this costs nothing at rest, needs no wire change, and covers busy, lost and reordered acknowledgements. The narrow alternative is to send the wake acknowledgement on `Persisted` when the follower woke while busy; that covers F1 only. Test: s2 as a deterministic case in simulation_test's `quiescence` namespace. |
| F2 | yes | gap | Question 4: the mutant removes exactly the rule (`peer.told = 0`), but the suite catches it by accident. Seed 1080 fails only because the deposition lands in the last few ms of settling. The new election-bound oracle never fires (the mutant's longest leaderless span is 24 ms against a 600 ms bound). Any change to how the test draws random numbers moves that timing, and the seed then silently stops catching the mutant. No deterministic test forces the interleaving that the docs describe. | job 261121-141837-c1601b log; driver_replace_test.cc:224 | Add s6 (probe_wake.cc) as a quiescence case in simulation_test, tagged `--fails-without` on `wake-ack-unanswered`. Optionally report the rest-exposure count in driver_replace_test, and make it an oracle once F1 and F3 close. |
| F3 | no | defect | The same exposure follows when both wake acknowledgements are lost (s5) or overtake the answers to the leader's round (s4). TCP keeps order per pair, so s4 needs a reconnect, and s5 needs two losses. The heartbeat-interval re-acknowledgement in F1 closes both. | probe s4, s5 | Covered by F1's rule. |
| F4 | yes | defect | A4: docs/mechanisms/quiescence.md:43-44 says rest "stops heartbeats only while every follower rests too; a follower's timer that runs is always fed". F1, F3 and the sweep's 222 spans show this is false at this commit. | quiescence.md:43; F1 | Make the doc true once F1 lands; until then, narrow it to the ordering the rule covers. |
| F5 | no | nit | Question 1 is sound. A stale-term answer is dropped (core.h:1193). A removed member is not in `peers_` (core.h:1197). A learner's acknowledgement costs one heartbeat round. Late or duplicated answers cost one round each, at a geometric rate. s3 shows no storm the fix introduces: under 100% duplication the leader never rests on either build, because any frame wakes a resting leader. | probe s3; core.h:1193-1209 | None. |
| F6 | no | nit | Question 3: an idle group still rests and stays at rest. After each scenario that ends at rest, 10 election timeouts send 0 to 4 messages: the one heartbeat round that rests the group. `quiescence::at_rest` passes under TSan. | bf5253; probe quiet windows | None. |
| F7 | no | nit | The journal's open point on token collision: the wake acknowledgement's rpc is the follower's own `token()` (core.h:563), and it can equal a leader flight's rpc. That is mostly benign, since the real answer then arrives unprompted. F1's re-acknowledgement makes it moot. | core.h:563 | None, or a zero rpc for the acknowledgement. |

## Not run

- CI at dbb22e7 (red from consensus_mutants, which the lead says is unrelated); A5 is not settled
  here.
- The fix's sweeps under sanitizers at dbb22e7 itself. The c45bb24 jobs stand for them because
  dbb22e7 only adds ctest entries.
- Learner and witness wake orderings beyond reading the code. The sweep's odd seeds include
  witnesses, and their exposure is counted with the rest.
- Whether F1 occurs outside the simulator: it needs a member's pre-vote or vote (a removed
  member, or a restarted member) to land while a follower persists the commit that rests it.
