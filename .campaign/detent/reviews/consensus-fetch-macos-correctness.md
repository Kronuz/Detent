# consensus-fetch-macos review: correctness

Commit: consensus:38f2022f10b0cedc5b1aaadac1a670433c568007 (diff against consensus:771e1bb)
Verdict: CHANGES
Date: 2026-11-16

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- 261116-012230-3e4d67 (A1, parent consensus:771e1bb, 12 SIGSTOP runs): 7 of 12 fail, at :1128
  (fetch error), :1154 (round trip) and :1155 (term). None at :1102 (commit past 2 s).
- 261116-012238-ba5d78 (A2, the same repro at the fix): 0 of 12 fail.
- 261116-012244-d61b39: `fetch-alongside-stalls` and `fetch-alongside-consensus`, 3 runs each, pass.
- 261116-012305-408699 (wall-clock mutant): killed.
- 261116-012259-238494 (late-deadline mutant): **failed, the mutant survived** (see F3).
- CI run 43839418972 at consensus:38f2022 (the lead's evidence): attempt 3 macos-x86_64 fails at
  peers_test.cc:1244; linux-tsan times out at 30.01 s on attempts 1 and 3. Main's green run
  43819925118 has linux-tsan `consensus_transport_peers` passing in 23.27 s.

## What I checked myself

Scripts are in `.scratch/review-consensus-fetch-macos-correctness/` (`mutant.sh`, `count.sh`, `show.sh`,
`build.sh` with the CI flags). The CI logs are under `ci/` in the same folder.

| Probe | Job | Result |
|---|---|---|
| P1 late-deadline mutant, `fetch-alongside-stalls` x10 | 261116-013029-5ddbae | killed 8 of 10; runs 9 and 10 pass |
| P2 blocking-reader (100 ms off-CPU sleep per served fetch) at the fix | 261116-013031-83870e | 3 of 3 pass, "stalled" 1700-1726 ms, one run's slowest bucket 1 s |
| P3 the same at main | 261116-013033-c01693 | 3 of 3 pass: the test never caught it, so this is not a regression |
| P4 no-cpu mutant (stall = lateness, no CPU subtraction) | 261116-013036-0fa8f4 | survives `fetch-alongside-consensus`, `-stalls` and `late-append` 3 of 3; 92-100 ms excused with no stop |
| P5 excused stall in us with no stop, x6 | 261116-013236-b32b3c | excused 276, 1521, 0, 1257, 614, 486 us: in 5 of 6 runs the bound is 256 ms, not 64 ms |
| P6 `fetch-failures`, 700 ms stop once the retried fetch is asked | 261116-014040-b977dc (baseline 261116-014038-452d50) | 3 of 3 fail at the CI's check (:1244, :1246 with the probe's lines); baseline 0 of 3 |
| P7 the retried fetch's error | 261116-014608-265de3 | "Operation timed out, 0 bytes": Peers' fetch_deadline, not a lane |

## Answers to the lead's questions

1. Transport fix. It is safe for liveness. Elections run on the Core's host-driven ticks, and
   late-append (consensus:771e1bb) is a Core change, so neither depends on a lane deadline.
   AppendObservation timeouts are only stats. "Once" is right: a second stop inside the
   extension fails the lane. Lateness comes from `steady_clock`, which counts SIGSTOP and VM steal
   (and, on Apple's libc++, I believe system sleep; I did not verify this). But nothing bounds
   the extension (F5). Reading pending bytes before failing would work across hosts, where the
   peer's bytes are already in the kernel. It would not work in the in-process cluster, where
   the sender was stopped too. A lateness extension capped at one `timeout_` covers both cases.
2. The test change is not "unchanged when nothing stops". Any excused stall above zero moves
   the round-trip bound up one bucket, from 64 ms to 256 ms. That happened in 5 of 6 local
   no-stop runs, and in CI attempt 3, which printed "stalled 1 ms" (F4). Off-CPU blocking on the
   executor counts as a stall, so the test cannot tell a VM stop from a handler that blocks
   (P2). The test never caught that kind of regression at main either (P3).
3. SIGSTOP faithfully reproduces :1128 and the round-trip check, and P6/P7 show the stop model
   also explains the new :1244. The fix leaves a second deadline uncorrected: Peers'
   `fetch_deadline` (F2). The term change is the most common local failure but has not been
   seen on CI. That fits the model, because CHECK aborts on the first failure and the term check
   comes last. :1102 has no recorded reproduction (F7). The TSan timeout is not a hang. Every
   scenario through `fetch-hung-peer` prints ok, and `late-append`, the last scenario, is cut
   off at the 30 s ctest limit. The new scenario adds 8.3 s to a binary that took 23.3 s (F1).
4. The mutants do not prove the fix. Late-deadline survives 2 of 10 runs, plus the
   contributor's own run (F3). Three mutants are missing: one for the uncovered fetch deadline
   (F2), one for an uncapped extension (F5), and no-cpu (F6).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | defect | linux-tsan `consensus_transport_peers` exceeds its 30 s ctest TIMEOUT on every run at the fix. It is not a hang: `fetch-alongside-stalls` adds 8.3 s, including 4.2 s of sleep, to 23.3 s, and `late-append` is cut off. A4 fails. | CI 43839418972 attempts 1 and 3 (linux-tsan); main 43819925118 at 23.27 s; CMakeLists.txt:121 | Split the peers scenarios into separate ctest entries, raise the transport peers TIMEOUT, or shorten the stalls. linux-tsan green is the test. |
| F2 | yes | defect | CI :1244 is a stop during the retried fetch in `fetch-failures`. Peers' fetch deadline timer (`await_reply`, peers.h:338, 500 ms in that test) fires late and fails the fetch with timed_out. The fix extends only `BasicSession::timed`, so a stopped process still fails fetches it would have finished. | P6 3 of 3 vs baseline 0 of 3; P7 "Operation timed out" | Apply the same late-timer rule in one helper used by both session.h `timed` and `await_reply`, and check the dial race deadline (peers.h:618) the same way. Test: a scenario that stops 700 ms after the retried fetch is asked (P6's `stop-retried`) passes. |
| F3 | yes | gap | The late-deadline mutant is not reliably killed: 8 of 10 in P1, and it survived the contributor's evidence job, which failed. The transport fix has no deterministic failing test (proof standard 2). The stall kills it only when a frame needs a later reactor turn after resuming. | P1 261116-013029-5ddbae; 261116-012259-238494 failed | Make the test deterministic, for example a stall while a frame larger than the socket buffers is mid-write, or a session-level test. Then the mutant job exits 0 on every run (run it 10 times). |
| F4 | yes | defect | With no stop, the round-trip check is weaker. `bucket_bound(64 ms + excused)` returns 256 ms for any excused above 0 ns, and timer slack leaves 0.3-1.5 ms excused in 5 of 6 no-stop runs. On the slow runner the test is meant for, a 64-256 ms round-trip regression now passes. | P5 261116-013236-b32b3c; CI attempt 3 "stalled 1 ms"; peers_test.cc:1197 | Count only material stalls, for example a wait late by more than a few ms beyond its CPU, or keep the 64 ms bound when excused is below a floor. Test: P5 prints a 64 ms bound on 6 of 6 no-stop runs. |
| F5 | yes | defect | The extension is the full lateness, with no cap (session.h:261 `expires_after(late)`). After a stop of any length (a debugger, a cgroup freeze, a long VM pause), a lane with a frame in flight to a peer that went silent meanwhile stays open for that whole length. The control lane has no other bound, so traffic to that peer queues behind it. | session.h:255-265; CHANGELOG "at most that lateness" | Cap it with `std::min(late, timeout_)`: the peer is owed at most one more deadline. Test: block the executor for 3x `timeout_` with a silent peer mid-frame, and the lane fails within about one `timeout_` after resuming. |
| F6 | no | gap | The no-cpu mutant survives everything. Without the CPU subtraction, 92-100 ms per no-stop run is excused and no test notices, so the ruling that "the executor's own work still counts" is unproven. | P4 261116-013036-0fa8f4 | Add a check that a CPU-bound handler (a spin of about 100 ms) adds no stall. |
| F7 | no | gap | The A1 evidence job reproduces :1128, :1154 and :1155, but not :1102 (commit past 2 s). That cause rests on unrecorded local runs. | 261116-012230-3e4d67 log | Add a stop placed with a command in flight to the A1 job, as `fetch-alongside-stalls` does. |
| F8 | no | nit | A stop that lands inside `pump()`, outside the 1 ms wait, is not counted in `stalled`, so ticks still jump. It is rare: the A2 repro passed 12 of 12. | peers_test.cc:365-378 | Measure the whole loop iteration, not only the wait. |
| F9 | no | nit | Off-CPU blocking on the executor (P2) is excused as a stall, up to a 1 s bucket. The test did not catch this at main either (P3). | P2, P3 | None needed for this quest; note it in the test's comment. |

## Not run

- macOS system sleep: I did not test whether lateness counts it (F5's laptop case). It rests on
  libc++'s choice of clock.
- No TSan run locally. F1 rests on the CI logs.
- No multi-host (real network) stop. The cross-host analysis in answer 1 is reasoning only.
- A3, three consecutive macos-x86_64 CI runs, is the lead's to run after the fixes.
