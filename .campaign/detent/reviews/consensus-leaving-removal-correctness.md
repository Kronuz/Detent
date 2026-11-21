# consensus-leaving-removal review: correctness

Commit: consensus:ebc3b2eb72b8215a93b6e6f33a88b7d09b5d61df
Verdict: APPROVE
Date: 2026-11-21

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

At ebc3b2e. It differs from the first reviewed commit, consensus:7e83a6b, only in tests and build.
consensus:822aa7c folds the review probe's checks into test/driver_replace_test.cc: the
`write()` stream is always on, and `settle_and_check()` matches probe_replace.cc. consensus:ebc3b2e
runs the 1000 ctest seeds as four 250-seed parts (seeds 1-1000, unchanged).

- 261121-031008-0547aa: A1 at ebc3b2e, 3000/3000 seeds with the by-value checks (66502 committed
  client writes checked, 801 elections between promotion and removal).
- 261121-031034-f6c862 and 261121-031035-8662fc: both mutants at ebc3b2e fence at seed 1 with
  "persistence lacks preowned reservation".
- CI 44895551866 at ebc3b2e: green, as the lead reports.

At 7e83a6b, where the code was reviewed:

- 261121-010545-420472: the test at 170db35 (pre-fix core) fences at seed 1 with "persistence
  lacks preowned reservation"; the test reproduces the bug.
- 261121-012226-2fe3f5: 3000/3000 seeds at 7e83a6b (994 replaced their leader, 505 a witness,
  1487 transfers). Same counts as 261121-011140-7b0e14 at 06621ad.
- 261121-012216-933a9e (removal-from-any-drive) and 261121-012218-e3e272 (removal-tick-unplanned)
  at 7e83a6b: both fence at seed 1 with the named reason; each gate is load-bearing (A2).
- 261121-011657-336f3f: suite subset at 27f79b0 (code identical to 7e83a6b except the mutant
  registration), 9/9 passed.
- 261121-012106-ce07f5: strata placement_repair replace seed 1 passes at consensus 20198fc
  (b8de3b8 plus 9a2476d), not at this commit (see F4).

## What I checked myself

Scripts are under `.scratch/detent/review-consensus-leaving-removal-correctness/` in the home
checkout: `probe_replace.cc` is driver_replace_test with a by-value oracle added, `run.sh` runs it,
`oracle_teeth.sh` runs a mutant against it.

- Probe 261121-012750-1033ca at 7e83a6b, 5000 seeds in each of 4 configurations, all passed. The
  configurations: the default; a client write stream on the leader through the whole replacement;
  a transfer rate 30 times the default; writes plus transfers. After `done` it runs 3000 more steps
  and then checks:
  1. The final membership equals the founding one minus the leaving member, plus the joiner in the
     leaving member's role, with `leaving == 0`.
  2. Every member holds that membership in force.
  3. Exactly one committed Configuration entry drops the leaving member, and exactly three are
     committed (add, promote, remove).
  4. The removed member does not lead the highest term.
  5. Every client write whose placement committed holds its payload, and every data member
     applied through it.

  Results: 110692 and 88096 committed writes checked by value. 717, 1324, 3479 and 2134 elections
  (by configuration) landed while the promotion was in force and the removal was not.
- Probe 261121-013608-996c50: a core mutant that keeps `leaving` set in the removal is rejected by
  membership validation ("membership leaving" fence), so the suite test and the probe both fail.
  The mutant does not tell the two oracles apart; F1 rests on reading the test.
- Reading the code, by question:
  1. Liveness. The Worker plans a leader Tick and steps it in the same `submit_retained` call
     (include/consensus/worker.h:712-731). `hold()` takes only a follower's Receive
     (worker.h:578-583). The internal `core_->step(current_time)` calls (worker.h:484, 492, 538,
     549) all run while the core is busy, and then `handle(Tick)` does not drive
     (include/consensus/core.h:490). So `removal()` at planning equals `removal()` in `drive`.
     The steps before it in `drive` (CheckQuorum, D4 step-down) can only turn it to nullopt; the
     unused pack is released once the core is idle (worker.h:1153). A new leader recomputes the
     removal from its own committed membership once its no-op commits, because `removal()`
     requires `log_term(commit) == term` and no uncommitted configuration. The election counts
     above show leader changes in that window, and every seed completed. A busy leader still gets
     its Tick admitted, under the ARCHITECTURE.md timer-debt rule; the write-stream runs
     completed every seed within the step bound.
  2. Other unreserved Persists. None. `drive(false)` (the call after a Persisted, core.h:526)
     returns early off a leader. A leader persists in `drive` only through `propose_membership`,
     which runs only when `tick` is set (core.h:1121-1123). `follower`, `start_transfer`,
     `replicate` and `timeout_now` emit no Persist. On a lone voter the commit continuation
     after the removal append uses the planned second (hard) reservation. The planned bytes are
     `storage_batch_size(false, true, 1, encode_membership(next).size())`, which is exactly the
     batch `propose_membership` writes (core.h:714-731, admission.h ChangeMembership). There is
     no joint configuration: a promotion's commit comes from an AppendResponse (planned hard),
     and its Persisted no longer proposes.
  3. Safety. A leaving leader appends the removal, and the commit counts only the new
     membership's voters (core.h:1220-1235). Its next drive hands off or steps down (D4,
     core.h:1109-1115). The probe's single-removal and removed-member checks hold over 20000
     seeds. "Superseded" in 06621ad concerns the "after" command, which the test may propose
     twice; the removal cannot be proposed twice, because once a removal entry exists,
     `committed != membership()` until it commits, and after the commit `leaving == 0`.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | no | gap | Addressed at consensus:822aa7c, which folds in the probe's checks. driver_replace_test at 7e83a6b checked: no fence, one leader per term, the same entry at each index on every node, and that every final data member applied one removal configuration and a later command. It does not check the final membership by value, that exactly one removal committed, that the removed member stops leading, or any client write made during the replacement. | test/driver_replace_test.cc:300-340; probe 261121-012750-1033ca adds all four checks and passes 20000 seeds | Optional: fold `settle_and_check` and the write stream from `probe_replace.cc` into the test. |
| F2 | no | nit | `removal()` omits two guards that `can_change_membership()` has: `!transfer_` and `!witness_self()`. A leader in mid-transfer, or a witness leader before its handoff, proposes the removal. The old `drive` condition behaved the same, and the transfer-heavy probe runs pass. | include/consensus/core.h:208-215 vs 202-205 | None needed; state it in D3 if it is intended. |
| F3 | no | nit | A leader Tick with a removal due now reserves two Control appends, so it can return Pressure and skip that Tick's heartbeat and CheckQuorum. Before this change a leader Tick never could. The window is bounded: the promotion already funded the same Control pack, and idle packs are released. The CHANGELOG's "at most a heartbeat" holds only when that Tick is admitted. | include/consensus/worker.h:714-727, 1153 | None needed. |
| F4 | no | gap | A3 ran at consensus 20198fc (a cherry-pick of 9a2476d onto b8de3b8), not at this commit, because strata pins consensus-quiescence's API. A4 is green at ebc3b2e (CI 44895551866). | 261121-012106-ce07f5; `campaign status consensus-leaving-removal` | The lead's call: rerun A3 at the quest head after consensus-quiescence lands, as the journal plans. |

Blocking: yes or no. Kind: defect (wrong behavior), gap (unproven claim or missing test), nit. A
finding that would remove or narrow user-visible behavior offers options, including keeping it,
and is never blocking.

## Not run

- CI and sanitizers (A4 is CI's).
- The full ctest suite at 7e83a6b; I relied on 261121-011657-336f3f at 27f79b0.
- Xapiand/strata A3 at this commit.
- A probe under storage pressure that forces a leader Tick to return Pressure while a removal is
  due (F3 is argued from the code, not measured).
- A mutant that tells F1's oracles apart. The one I tried (261121-013608-996c50) is caught by
  validation first.
