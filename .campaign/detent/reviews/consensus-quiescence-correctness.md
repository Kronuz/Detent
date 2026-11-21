# consensus-quiescence review: correctness

Commit: consensus:a020e144f9f13bb3578a3428b23fe462a1a721e1
Verdict: CHANGES
Date: 2026-11-21

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

Safety holds. Rest never lets a deposed leader commit or serve a read, and a reopen resumes only
the exact durable hard state, so it can never fall behind a ballot it cast. Liveness has two
holes, which my probes reproduce:

1. A leadership transfer to a follower at rest leaves the new leader at rest, with its term
   entry unsent.
2. A leader whose group fences while its node stays up is never replaced: no LeaderGone fires,
   and no follower at rest has a timer.

## Evidence I relied on

- 261120-225122-c34e4e (TSan, a020e14): `closing` and `quiescence` cases ok; simulation 300 seeds
  x 10,000 steps, oracles clean (31,340 closes, 16,389 resumed, 320 stale, 14,768 LeaderGone);
  driver simulation 30 seeds; `reopen_barriers` 36 checks, `leader_gone` 16 checks. Log read.
- A2 mutants killed at their mutant commits: 261120-105231-8e1e83, -105233-efd55f,
  -110213-1b3000. A1 fails-without 261120-095359-890b19.
- A4 0760b8 / 5cedce, A5 eb821c, A6 e76794 / 17e4ea / 8fede5, accepted as they are stated. Xapiand's
  6,984 B is also a lone voter (Xapiand:d5b84cdf4 journal, "0 for an idle lone voter"), so A6
  compares like with like.
- CI 44877489407 at a020e14: every job is green except linux-tsan, which was still running its
  rerun when I checked. That is the lead's gate, not a finding here.

## What I checked myself

Probe 261121-005139-cd50f8 at a020e14 (light). The scripts are in
`.scratch/detent/review-consensus-quiescence-correctness/`: run `run.sh probe.cc`. They use
simulation_test's `closing::Group` harness and compare each scenario against the same scenario
without `quiesce`.

- P1, transfer at rest. With all three voters up, the new leader's term entry reaches the
  followers one tick late (control: within the settle). With the third voter down, the target
  leads term 2 with `resting 1, timer none`. Its entry reaches the old leader only after 780
  units, when that leader's own election timer wakes it (control: at once).
- P2, leader fenced at rest. The followers never elect in 19,400 units (16 x election_max).
  Control: they elect after 970.
- P3, leader at rest partitioned, followers elect term 2 on LeaderGone. A Read and a Propose wake
  the old leader. It serves no read, commits nothing, and steps down 300 units after the wake.
  This one is safe.
- Code reading:
  - `wakes()`, `wake()`, `leader_at_rest()` and the `told` bookkeeping. A follower that missed
    the commit-carrying append keeps the leader awake.
  - `reopen()`: exact match against `durable_`. `closed_state()` takes `durable_`, and when the
    Core is not busy its in-memory hard state equals it.
  - Driver `step()`: `run_one(tick)` steps a fresh Tick before `admit()` submits frames, notices
    or calls, so `now_` is current when a group wakes.
  - `leader_gone()`: it reaches only followers of a leader on that node.
  - Transport: the only timers are per-peer (`RpcPeer::timer`, host_transport.h:808). There is
    no per-group timer.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | defect | `resting_` survives TimeoutNow. `receive_message(TimeoutNow)` does not go through `follower()`, and `campaign()` and `lead()` never clear `resting_`. The follower at rest campaigns and becomes leader still at rest: `drive()` returns early (core.h:1241), and `next_deadline()` is maximum. Its no-op stays unsent until some frame arrives. With one voter down, that is the old leader's election timeout (780 units). If every vote request is lost, the candidate never retries. | 261121-005139-cd50f8 P1 "quiesce, third down": `resting 1 timer none`, 780 units; core.h:1012 (TimeoutNow campaigns), 1311 campaign(), 1332 lead() | Clear `resting_` in `campaign()` (and in `lead()`). Add simulation_test `quiescence` case: transfer at rest with the third voter down, and the target's term entry is on the old leader within the settle. |
| F2 | yes | gap | A fenced (or otherwise stopped) leader group on a live node is never replaced. Followers at rest arm no timer, and the node-pair detector sees a live connection. The doc's premise, "a follower at rest under a leader it cannot reach elects only on LeaderGone", leaves group-level loss undetected. Before quiescence, the followers elected within a timeout. | 261121-005139-cd50f8 P2: quiesce NO in 19,400 units, control 970 | Owner's call. Options: (a) on a fence, the leader's host tells its peers per group (for example the transport's `closed` frame raising `Wake` there), so the followers elect; (b) the host contract is to restart or close a fenced group at once, documented in quiescence.md and tested through the Driver; (c) a long backstop timer per follower at rest, costed in A4. Add the P2 case to the suite with the chosen fix. |
| F3 | no | gap | Random coverage of rest is thin: 20,375 node-steps at rest of about 5.5M in the quiescent seeds (about 0.4%). Transfers, fences and membership changes rarely meet a group at rest, which is how F1 got through. | c34e4e log | A rest-heavy seed mode (quiet stretches before faults), or deterministic cases per wake source. |
| F4 | no | nit | `heard_from_leader`'s `resting_ && source != leader_` clause is unreachable. `step()` wakes a follower on any (pre-)vote from a non-leader before `handle()`, and `wake()` sets `leader_contact_ = now_`, so the time clause already refuses. A mutant removing the clause survives. | core.h:391, 529-538 wake(), 1025-1027 | Drop the clause, or make the comment describe the mechanism that actually refuses. |
| F5 | no | nit | The `Driver::closed_state` comment says "at rest", but neither it nor `Core::closed_state` checks `resting_`. Closing a non-resting group is safe (the reopen only costs an election). | driver.h:259-263, core.h:297-301 | Fix the comment, or require rest. |
| F6 | no | gap | A4 counts only the leader's side. Follower groups at rest are shown to arm no timer in tests (`quiescence::rest`, groups_test `quiescent`), but not counted at 1M. LeaderGone staying silent through a long rest (past `dead_after` or the 60 s Bulk idle) is shown only for 3 s in groups_test. | quiescence-idle-cpu.md "What is not counted"; groups_test.cc:1321 | Optional: a follower-side idle row, and a longer rest with `stats().gone == 0`. |

## Answers to the lead's questions

1. Safety: yes. A deposed leader at rest commits nothing and serves no read (P3). A follower that
   missed the quiesce keeps the leader awake through `told`. The reopen's term and ballot are the
   recovered durable ones, used only on an exact match, so they can never be behind a cast vote.
2. Liveness: proposals, reads, membership changes, PeerLimits, Repair and frames wake a leader,
   and LeaderGone and (pre-)votes wake a follower. There is no lost wakeup in the Core or Driver
   ordering. The exceptions are TimeoutNow (F1) and a group lost without its node (F2). The
   install-sender-crash case is consensus-driver-install's code (not at a020e14). Here the
   receiver is not at rest during an install, because every append, keepalive or refused, goes
   through `follower()` and clears `resting_`.
3. LeaderGone fires only when a Control connection ends, and only at followers of a leader on
   that node. A normal rest ends no connection.
4. Yes for what they claim: no per-group timer in the transport, and A6 is a lone voter against a
   lone-voter figure. The scope limits are in F6.
5. Mostly. The mutants that matter for A2 are killed. F1 and F2 needed deterministic cases, and
   the random sim rarely reaches them (F3).

## Not run

- No probe for a witness group's rest. I did not establish whether a leader with a witness ever
  rests.
- No real-socket test of long rests, a partition without RST, or Linux. No rerun of A4/A6, and
  no TSan on the probes (they are single-threaded Core cases).
