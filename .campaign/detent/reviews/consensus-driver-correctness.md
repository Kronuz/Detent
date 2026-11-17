# consensus-driver review: correctness

Commit: consensus:57745e8541d7101c82b8f3f1bdd5472397875edf
Verdict: APPROVE
Date: 2026-11-17

Round 1 reviewed consensus:ff0cdbc (CHANGES, F1 blocking). Round 2 reviews only the delta
consensus:ff0cdbc..57745e8 (ccc9a3e, 41ee584, bf66941, 57745e8): F1 is fixed at the cause with a
real test; R16, R17, the waiter split and the new mutant commands hold. No blocking finding.

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

Jobs at consensus:ff0cdbc, accepted as recorded (not rerun): suite 261116-190312-344e41 and checks
261116-190312-87fdc0 (A1: D8 rows, simulation 8 seeds with witness oracle, waiter cases at 500k);
A2 261116-190312-6f3223 (lone voter, create and recover halves, no clock advance); ASan+UBSan
261116-190313-30053b and ASan close cases 261116-190313-51503a; TSan 261116-190313-c3161a (blocking
entry, waiter threads 300 rounds, driver rows); the four mutants killed 261116-190314-982145,
-f367e1, -15400c, -421b11 (complete/hop resume-before-unlink, destructor-leaves-linked,
close-frees-with-waiters); idle exclusive 261116-190315-47fde6 (macOS: three-voter member 7,288 B,
lone voter 7,262 B, 0 threads/descriptors/frames, 1.000 wheel entries per member). CI run
44008816987 green on all 9 platforms; on linux-x86_64-gcc `consensus_driver_idle` passed, so the
Linux 8,192 B gate held (ctest prints no number).

## What I checked myself

Probes: `.scratch/review-consensus-driver-correctness/probe.cc` built into the job checkout by
`run.sh` (Release); jobs 261116-195059-b2b769 (all cases) and 261116-195235-411d85
(`CASES="stalled_heartbeat close_after_stop"`).

1. D2/D8 completions (Q1). Every waiter completes through `complete` (unlink, claim, post),
   the cancel hop (claim, unlink on the loop, post), `shutdown` (unlink, claim, post) or `fail`;
   the claim word gives one resume. `ProposalPlaced` is deferred until persisted (core.h:711,
   773); `committed` checks `term_at` once the commit index passes (correct by log matching;
   `compacted` below the base); reads are Rejected on step-down (core.h:453) and cleared with
   `fail_all` on fence; transfer settles on the target leading, another leader, or the deadline.
   Checkpoint: `checkpoint_.reset()` and Core's `CheckpointPublished` happen in one `run_one`,
   so settle never mistakes a publication for a discard; `TooLate` means bundle phases, after
   every application chunk was offered. Close with a checkpoint waiter on held IO: `closed`,
   close completes, nothing published (probe `close_checkpoint`). After a reactor `run()`
   stopped, `close` never completes: F1.
2. Waiters (Q2). Resume follows unlink on every path above; a pending hop for a waiter close or
   shutdown already unlinked sees `owner == nullptr`. `Worker::runnable()` (R10) covers every
   `run_one` branch that does more than submit a Tick (persist_, done IO, completion_,
   application_, activation, held_, maintenance, preparation, timer_blocked_); each `Idle`
   return clears the flag that produced it, so no Idle-only spin, and an Idle turn with records
   to persist sets `persist_`, which runnable counts. After `consensus::run`, `rearm` resets
   `stopping_` and the shutdown source; watches end on the old token (`Wait::await_ready`
   checks it), so `watching`/`in_flight_` return to 0. Shared-store appends and publications are
   never launched (worker.h:230), so the stop phase cannot fence a group through a cancelled
   InlineCompletion run.
3. D5 (Q3). The entry drives, commits staged turns before checking `finished`, waits only while
   `armed_timers() > armed_group_timers()`, else throws `stalled`; the stop phase commits what
   the shutdown leaves staged. A4 literally (a lone voter plus a learner: a leader with its
   heartbeat armed, no other voting member): `stalled` in 0 ms with 1 group timer armed (probe
   `stalled_heartbeat`). `needs_peer` counts witnesses (driver.h:533), but every valid
   membership with a witness has another data voter (membership.h:110-114), so the witness
   clause is unobservable; the 2 + witness test is the strongest possible.
4. Lone voter (Q4). `campaign` persists term and vote with `elected` set and `become_leader`
   throws while busy (core.h:1127-1130): leadership follows a durable vote. A crash before that
   persist reuses only a term nobody else saw (a lone voter sends nothing). Recover leads term 2
   above the recovered log (driver_test lone_voter).
5. Y9 lazy state (Q5). Channel input is the 12-byte prefix until the length is known, then a
   buffer of the frame's length; every `process` reads it only at `used == frame_length` (> 12);
   `drop_input` frees it on every reset path; `optional` to `unique_ptr` keeps the same guards
   (an unguarded use would not compile against `has_value`/`value`). Driver `founding` and
   `activating` are read only behind their checks. The bench's channel figure is honest: the
   constructor builds the per-peer channels (snapshot_channel.h:41-44), 1,000 instances defeat
   tcache, and per-group bytes are averaged over all groups created.
6. Witness (Q6). Core delivers `Committed` only through `durable_.commit_index` and never while
   busy (core.h:1214-1223), so the Driver's acknowledgement (driver.h:811) never covers a
   non-durable entry; `Worker::applied` requires `through == delivered_`. The same incarnation
   can leave the witness role, though: F2.
7. Repin (Q7). journal 7c08540 at CMakeLists.txt; `migrate`, `layout`, `finish_migration`,
   `migrating` gone from worker.h and the tests; no code reference remains. ARCHITECTURE.md:101
   still describes the old constructor and R20 migration: F6.

## Round 2 (consensus:57745e8): evidence and checks

Evidence accepted as recorded: F1/F2/F5 tests at ccc9a3e 261116-200811-c09651 (driver_test `fence`
12 checks, `sink_throws` 4, membership_test 135, 0 failures); mutants killed, read from their logs:
close-after-stop-links 261116-200759-6d46f3 (`fence`: "close after shutdown completes with
task::cancelled" and "links no waiter" FAIL), sink-throw-unfenced 261116-200803-9752be (both
`sink_throws` checks FAIL), witness-becomes-data 261116-200807-369184. At 57745e8: suite
261116-233050-28a49e and 261117-000432-6d02da, ASan+UBSan ctest with the 11 waiter entries
261116-233053-a64e8a (all 11 ran and passed), ASan close cases 261116-233057-b2e74f, TSan
261116-233102-3f74e5, A2 261117-000456-6f48af. include/ is unchanged from ccc9a3e to 57745e8.

1. F1 (Q1). `close` now fails `task::cancelled` when `stopping_` right after `enter`
   (driver.h:341), the same check `Call::begin` makes (driver.h:410) and the cause: close never
   reached it. The test is real: at ff0cdbc's close (mutant 6d46f3) close does not complete and a
   waiting group remains; at the fix both pass. DRIVER.md states the shutdown order.
2. R16 (Q2). The rule sits only in the leader's `handle(ChangeMembership)` (core.h:673-678);
   recovery and followers apply committed configurations without it, so no recovered log is
   refused. It adds one reachable refusal: witness to learner. Witness to voter was already
   refused (`changed += 2`, membership.h:134), and a removed witness cannot return under its
   incarnation ("reuses an incarnation", membership.h:128), so no path bypasses it; learner to
   witness stays allowed. Simulation (8 seeds plus driver simulation) passes in the suite.
3. R17 (Q2). `Worker::fence` is the existing `fail` (worker.h:1120), the path a storage fault and
   `application_failed` take: an IO completion arriving after it is ignored (`failed_`,
   worker.h:234), the Sink hears `Fenced` once (`fence_seen`, driver.h:812/829), and a Sink that
   throws again on `Fenced` is caught with no second fence. No ordering is new.
4. Waiter split (Q3). Each `explore` builds its own `Explorer` (seed 1) and the limits are the
   old ones (20,000; threads 100). Probe job 261117-001610-a7b6f2 (`waiter_split.sh`) compiled
   ff0cdbc's waiter_test.cc beside 57745e8's and ran the old no-argument run and the 11 ctest
   commands: every spec's schedule line is identical and the runs sum to the same 129,439.
5. New mutant commands (Q4). From their logs, none stops on an unknown case:
   complete-resumes-before-unlink 261116-233131-9cd406 and hop-resumes-before-unlink
   -233135-802056 abort in ASan inside `explore` of the first case, rows-propose
   (driver.h:555 `unlink`, driver.h:462 `Cancel::hop`); destructor-leaves-linked -233139-3d5a9c
   runs rows-* and close, close-read (119/29/514/20,000/20,000/20,000 schedules) and fails in
   teardown ("a destroyed frame's waiter stays linked"); close-frees-with-waiters -233142-a02ec6
   runs rows-* and fails in close at run 10,029 ("a proposal completed with no placement").

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | defect | `close()` after the reactor `run()` shut down never completes: it skips the `stopping_` check `Call::begin` makes, links its waiter and marks the group ready on a loop nothing steps. DRIVER.md says calls after shutdown complete with `task::cancelled` (propose does), and its `~Driver` precondition ("after run() returned and every group closed") invites exactly this order. | probe `close_after_stop`, jobs 261116-195059-b2b769 and 261116-195235-411d85: propose after stop `task::cancelled`; close `done 0`, group state still held, 1 waiting group; driver.h:337-348 vs :408 | `close` completes `task::cancelled` when `stopping_`; DRIVER.md states the shutdown order (close before stop, or close after stop is cancelled). Test: driver_test `fence` case, close after shutdown completes with `task::cancelled`. |
| F2 | no | gap | A witness member's ranges are acknowledged by the Driver (R7), and `validate_change` accepts the same incarnation going witness to learner and then learner to voter. That member's Core has applied every acknowledged range; its application never saw one, and the Sink's next `Committed` starts mid-log with no install. | probe `witness_to_learner`: both changes "accepted", direct witness to voter refused; driver.h:811; membership.h:131-135; Core checks only log match on promotion (core.h:666) | Owner's call: (a) refuse witness to non-witness for one incarnation (a witness leaves only by removal; narrows Core's API); (b) fence, or require an install, when a member that acknowledged as a witness stops being one; (c) keep, and state in DRIVER.md that a witness incarnation never becomes a data member. |
| F3 | no | gap | A3 claims close with linked waiters of every kind; waiter_test links propose, committed, read and create, driver_test `close` propose, committed and read. Transfer, repair, checkpoint and change_membership are never linked at a close in the suite (my probe covers checkpoint: correct). | test/waiter_test.cc:146, 430-441; test/driver_test.cc:366-388; probe `close_checkpoint` | Add transfer (3 voters), repair (held turns) and checkpoint (held IO) waiters to a close case. |
| F4 | no | nit | A cancel whose loop scheduler declares no `post_call` runs the hop inline on the cancelling thread (`post_call` returns false only then), unlinking from the group list off the loop: a race, not the documented protocol. | driver.h:453; task.hh `scheduler_ref::post_call` | Refuse a scheduler without `post_call` in the constructor, or document the requirement. |
| F5 | no | nit | A Sink that throws inside `dispatch` lands in `step`'s catch: waiters complete `fenced`, the Worker is not fenced, and the rest of that action batch is dropped (a lost `Committed` leaves Core holding a range nobody will acknowledge). | driver.h:638-640, 792-817 | State that the Sink must not throw, or fence the Worker in that catch. |
| F6 | no | nit | ARCHITECTURE.md:101 still says `Worker(io, TurnDriver&, group, ...)` and that a per-group journal migrates into the turn log (R20); both are gone with the repin. | ARCHITECTURE.md:101 | Describe the `journal::Host&` constructor; drop the migration sentence. |
| F7 | no | nit | blocking_entry_test `stalled` arms an election timer (a three-voter follower), not the heartbeat A4 names; a lone voter's leader arms none. The literal case holds (my probe), but the suite does not carry it. | test/blocking_entry_test.cc:117-138; probe `stalled_heartbeat` | Adopt the lone voter plus learner case. |
| F8 | no | nit | The Linux Y9 figure is a pass/fail only: ctest prints nothing on pass, so the journal's "~7,000 expected" is unrecorded. | CI 44008816987, linux-x86_64-gcc log | Print driver_idle's output in CI (`ctest -V -R driver_idle`) or record a Linux job. |
| F9 | no | gap | The A1 driver checks job at 57745e8, 261116-233128-09ff0c, failed: `consensus_driver_idle 1000` hit driver_checks.sh's 300 s alarm (exit 142). Every other case passed; the bench code is unchanged since ccc9a3e, where it passed (261116-200753-e0161a), and it passes in suite 261117-000432-6d02da and ASan ctest (77 s); disk and load, like R18. | job 261116-233128-09ff0c log; test/driver_checks.sh:18 | Rerun the checks job, or raise driver_idle's alarm as R18 did for the Worker, before citing it for A1. |
| F10 | no | nit | membership_test `witness_stays_witness`'s second check (witness to voter) is refused by validate_change's voter-witness rule, not R16; under the mutant it fails only because the first change was accepted. The learner check alone tests R16. | test/membership_test.cc:508; membership.h:134; job 261116-200807-369184 | None needed; or say so in the check's message. |
| F11 | no | nit | R16 is a role-transition rule placed in Core beside `validate_change`, its one caller, which already holds the per-member role rules. | core.h:673-678; membership.h:121-140 | Move the check into `validate_change` (one place). |
| F12 | no | nit | DRIVER.md:73 still says `~Driver` runs "after run() returned and every group closed", while :143 and the F1 test (a Driver destroyed with group h open after stop) say it frees what groups hold. | docs/DRIVER.md:73, 141-143; test/driver_test.cc `fence_and_shutdown` | Drop "and every group closed" from :73. |


Round 2 status of round 1's rows: F1 fixed (ccc9a3e; test `fence`, mutant 261116-200759-6d46f3).
F2 resolved by R16, option (a). F3 fixed for linking: `close` links change_membership, transfer,
repair and checkpoint waiters, queued and not admitted (an admitted transfer or repair at close
stays untested; my round 1 probe covered an admitted checkpoint). F5 fixed by R17 (`sink_throws`,
mutant 261116-200803-9752be). F6, F7 fixed. F4, F8 left by the lead.

## Not run

No sanitizer build of my probes (they are Release; the contributor's ASan and TSan jobs cover the
suite). No reproduction of a linked transfer or repair waiter at close (F3), of a checkpoint
cancelled after publication started reaching the Sink, or of a detached checkpoint orphaned by a
blocking entry's own stop (my attempt, probe `entry_orphan_checkpoint`, finished every checkpoint
inside the entry, so the case did not arise). No Linux run of the idle bench beyond CI's pass. No
review of the transport (consensus-transport) or Xapiand's group-core use of the contract.

Round 2: no probe of a Sink throwing while its group is closing (`Worker::fail`'s closing branch
keeps `io_job_`); no rerun of the failed checks job (F9); no Linux or CI run at 57745e8 (CI not
run at head).
