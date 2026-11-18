# namespace-s6-hardening review: correctness

Commit: Detent:9054a5fdd3573a0d38207f6989b21b1791a77b61
Verdict: CHANGES
Date: 2026-11-17

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

The head adds only the journal on top of Detent:bfcb4159 (`git diff --stat bfcb4159 9054a5fd`:
one file, `.campaign/detent/journal/namespace-s6-hardening.md`), so every job at Detent:bfcb4159
stands for this commit.

## Evidence I relied on

- 261117-075322-afbd3a and 261117-075322-b090a5 (the A1-A4 tests plus relay mirror), exit 0; pins
  261117-075327-25508b, exit 0.
- Mutants, each exit 8 (the test failed without the fix, as it must):
  - 261117-075322-9e1c41: early apply, publishing at prepare (A2).
  - 261117-075322-55a56b: no staging charge (A1).
  - 261117-075322-92def8: no directory-row admission in the decoder (A1).
  - 261117-075323-d98365: no Dropped request (A3).
  - 261117-075323-bc058a: an unfreezable namespace omitted (A4).
  - 261117-075323-0f145c: FramesBehind mapped to distribution_unavailable (A4).

  Each mutation removes exactly the mechanism its criterion names. The tests check values: whole
  `Store` equality (namespaces, change words, directory cursor, kinds), `ledger->used() == 0` and
  `peak() > 0`, the Dropped cursor, and the exact 503 body.
- Native gate 261117-075327-c3d9ec failed 2 of 275: `test_authority_transport_peers`
  (`committed && leader`, test_transport_peers.cc:168) and `test_authority_multivoter` (a read
  timeout on `/v2/open`). Neither test reaches the changed code. Both passed 3 of 3 alone in
  261117-083302-8cad80, so I read them as load flakes. The gate still has to be green before
  landing.

## What I checked myself

Scripts are under `.scratch/review-namespace-s6-hardening-correctness/` in the home checkout.

- `probe.sh` with `probe_lagging_drop.cc`, job 261117-080935-ab7b1c (exit 1 means the defect is
  present; exit 0 once fixed). See F1.
- `rerun_gate_failures.sh`, job 261117-083302-8cad80: the gate's two failures, 3 of 3 passed.

By reading, against the four questions:

1. **Staging charge.** `StagedFrames` (namespace_session.cc:380) charges each prepared frame's
   growth to `options.limits.storage_account` after every frame, piece and record call. It charges
   the directory copy in one `admit` (namespace_session.cc:388). `admitted_limits` is idempotent,
   so the decoder and the stage share one ledger.
   - All charges sit in RAII tickets owned by the coroutine's locals, so failure, refusal,
     cancellation and peer loss all refund. Corruption and refusal are tested to
     `used() == 0`; cancellation and peer loss are by construction.
   - There is no double charge. The decoder's directory copy and the stage's copy are two real
     copies, and the decoder releases its own at `publish_directory`.
   - The directory is never published partly. The decoder admits all rows at the count
     (session_frames.cc:617), admits each name as it arrives, and hands the update on only when it
     is complete. The stage holds it until `publish()`.
   - On the encoder side, the plan is admitted once at construction and each piece's readers as
     the piece opens. Both are released at `next_frame` or at piece end.
2. **`select_answer`.** The checks run in the order history.hh states: Ahead first for a
   same-cluster store above head whatever its incarnation, then FullCopy, Current, Entries, cut,
   Blocks. The F1 case through a history stays fixed (`test_authority_lagging_incarnation` in
   b090a5).
   - `AnswerKind::Dropped` comes only from the encoder's `request.dropped` branch
     (session_frames.cc:367), never from `select_answer`.
   - The relay's `RingSink` treats a kind-4 frame as UpstreamMoved, which is safe.
   - The drop signal itself repeats F1's mistake one level up: see F1 below.
3. **503 mapping.** `answer()` runs inside the per-task `try` (service.cc:2670). `FramesBehind` is
   thrown only by `plan_frames` (frames_export.cc:42). `frames_failure` matches it by exact
   `dynamic_cast` (service.cc:2688), and every other exception keeps 503 distribution_unavailable.
   The client maps 503 `source_behind` to `SourceBehind` (client.cc:427). Nothing else reaches it
   by accident.
4. **Tests and mutants.** See the evidence above: all six kill what A1-A4 claim.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | defect | A voter that has applied a drop but not the re-creation that followed tells a store already at the new, live incarnation that the namespace was dropped. `plan_frames` sends Dropped for any `store` once the entry is not live. It does not check the store's cluster or position against the drop, which is the cutover's F1 (identity before position) applied to the new frame. The store then forgets the namespace and stops following it (the documented contract and the test `Store`). Unlike F1's full copy, nothing heals this: an `all` cursor already past the re-creation never brings it back. v2 answers from applied state on every voter, so a follower or a new leader reaches this. | Job 261117-080935-ab7b1c: leader head at the new incarnation is 13; voter 2 holds the drop at 11; the store at 13 gets a Dropped frame. frames_export.cc:43 | A same-cluster store whose position is above the drop's (`store->position > entry->position`) gets no frame, the Ahead rule. Test: `probe_lagging_drop.cc` as a suite test (exit 0), and a mutant that removes the guard. |
| F2 | no | gap | The Dropped path never runs through the HTTP service. `test_authority_frames_service` is unchanged, so `FramesExport` with a null history, its `empty_`, and the guarded `held_back` loop are covered only by reading and unit pieces. | service.cc:2603-2605; frames_export.cc:14-19 | A dropped followed name in the frames-service test, checked byte for byte. |
| F3 | no | gap | On success the stage's ticket is refunded when `next_namespace_session` returns, so published generations leave the ledger. The client's long-lived memory is then outside `storage_account` unless the preparer charges it. | namespace_session.cc:442 | P4's agent preparer should charge what it publishes, or the contract should say who does. |
| F4 | no | nit | `publish()` is not atomic. `preparer.directory` and each `Prepared::publish` may throw part way, which would publish a prefix of the response. | namespace_session.cc:406 | Make `Prepared::publish` noexcept in the contract, or document the rule for P4. |
| F5 | no | nit | The charge follows growth (`charge()` runs after the preparer's call), so the ledger can lag real memory by one record before it refuses. | namespace_session.cc:414 | None needed; or ask the preparer for the size before growing. |

## Not run

- Sanitizers and the Linux matrix (CI's), and CI at 9054a5fd, which was still running.
- Liveness of A4's 503. I did not measure how long `FramesBehind` lasts in practice, for example
  between enabling and the first seal with positions on an idle cluster, where seals are
  count-driven. The service also answers 503 at once instead of waiting out the next's `wait` for
  the seal, as it already does when history is not ready.
- The full native suite again at this commit.

## Round 2

Commit: Detent:94d7cc09b5029c73e0c31eaf329a7ee49f6d15c6
Verdict: APPROVE
Date: 2026-11-17

### F1 is fixed

`plan_frames` sends Dropped only when the store is not a same-cluster store above the drop's
position (frames_export.cc:43-44, Detent:892e121b). That is the Ahead rule, matching
`select_answer`.

`test_authority_namespace_history` now lags voter 2 across the re-creation and checks two stores.
The store at the new incarnation gets no request; the store of the dropped incarnation is still
told. This is my probe's scenario, now in the suite.

The mutants kill the fix and its neighbours (each exit 8):

- 261117-122502-2e928a: the guard reverted to `if (store)`.
- 261117-122454-36ba59: no Dropped request.
- 261117-122509-9c0de7: an unfreezable namespace omitted.
- 261117-122514-0cd4cd: FramesBehind mapped to distribution_unavailable.

The tests pass:

- 261117-122519-346027 and 261117-144103-d40cc6, the A1-A4 tests (the second with the relay).
- Native 261117-122525-281a5f, 275 tests, green this time.
- Pins 261117-122525-4f8dea.

### Why jobs at older commits count for this head

The jobs ran at Detent:a045c1e8 and Detent:cb469446. Outside `.campaign`, those commits have the
same code as the fix commit Detent:705de218 (an empty `git diff`).

The head's only code change after them is merge Detent:88c5d57a. It brings in nine files from
delta-plan-fixed-cost, all under `src/detent_local`, `tests/detent_local` and `docs/perf`. None of
them is in this quest's file set, and this quest's files are unchanged from Detent:a045c1e8 to the
head. CI on the pushed head covers the merged tree; at review time it had not run.

### The other round-1 findings

F2 to F5 were non-blocking and are unchanged. The journal records the lead's ruling on F2: it was
dropped, leaving the Dropped path untested through the HTTP service. F3 (published memory leaves
the ledger) and F4 (`publish()` is not atomic) carry to P4's agent preparer. F5 is a nit.

### Not run

- My probe at this head: the suite test above reproduces it, and mutant 2e928a proves that test
  fails without the fix.
- CI at Detent:94d7cc09.
