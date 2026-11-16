# journal-driver review: durability

Commit: journal:535e9e52f37105621ba1e1b023149880b90bb0b5
Verdict: CHANGES
Date: 2026-11-16

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

Lens `durability`, carrying the lead's correctness and concurrency questions (Q1-Q5). Read: the
quest file, PLAN.md D96, D97, D105, the journal (R1-R22), journal `docs/design/host-layout.md`,
ARCHITECTURE.md "One directory per host", and `git diff origin/main..535e9e5`.

## Evidence I relied on

- A5 grep 261116-134559-d0bfa1 (passed at 535e9e5) and A3 idle cost 261116-134559-716069 (met).
- The suite, ASan and TSan reruns at 535e9e5 (261116-134558-0ebf97, -ff571e, 261116-134559-096532)
  and the mutant reruns were still queued; I relied on none of them. My probes build the test
  tree at 535e9e5, so they show the code compiles and the placement helpers behave.

## What I checked myself

Scripts under `Detent/.scratch/review-journal-driver-durability/`; each reruns as a job at a fix
commit (`sh run_probe.sh`, `sh run_close_probe.sh <dir>/close_probe_test.cc`).

- 261116-140207-14fdf6 (`probe_test.cc`, reuses `placement_test.cc`'s helpers):
  - `first_open_cut`: a power cut at each of 138 steps of the first open over fresh damage (no
    re-partition, between the new epoch's open record and the table's lost mark included), two
    restarts each, then 60 turns of retention and another restart: 38,915 checks, 0 failures.
  - `repartition_cut`: the same at each of 350 steps of 1->3 and 1->2 re-partitions that first
    see the damage (step 1's mark not yet durable): 212,789 checks, 0 failures. Q1 holds for ids
    the log names lost, after commit, after a crash at each step and across retention.
  - `absent`: a group whose one checkpoint record is acknowledged, then destroyed. Modes 1
    (segment zeroed past its first block) and 2 (segment gone) recover `damaged=1`, the group in
    neither map, and `Host::status` returns `none`: F1.
- 261116-140554-3cc85e (`close_probe_test.cc`, reuses `host_test.cc`'s helpers):
  - `close_failure`: ENOSPC on the close's shard write, then 200 appends of another group, then a
    restart: status `none`, and `create` makes incarnation 2 and removes both of the group's
    acknowledged artifacts: F2.
  - `recover_twice`: a group closed, reopened, checkpointed "new" with one batch, then recovered
    by a second Store in the same host: recovery returns bundle "old" with no batch, and the next
    turn fails the loop's driver ("turn record out of its group's sequence"): F3.

Settled by reading:

- Q1 ordering (host.h:405-502): leftovers of any other generation go before anything reads; the
  stored generation is read with `TurnLog::read` (writes nothing); lost ids are committed (pending
  file synced, renamed, directory synced) before the new generation is written; each new turn is
  synced; `layout` is replaced and the directory synced before the old logs go. The cut probes
  above confirm it.
- Q2 lock: `OwnerLockMode::directory` is `flock` on the directory descriptor
  (completion:7731315 `posix.h` `lock_owner`), per open file description, so a second Host in the
  same process is refused as in another; `PrefixIO::acquire_owner` hands out a token, so a group's
  Store never takes a second lock. Names: `g<16x>-`, `l<16x>-<4x>-`, `groups-<3x>` and `layout`
  cannot parse as one another (`groups-` is not hex after `g`); incarnation is in every artifact
  name; `remove_artifacts` and the views name only `group_prefix(group)`. Admission and
  reclamation probe `[mark, high)` by name (host_test `reclaim_isolation`, `census`); a closed
  group reopens from the shard's record (`closed_reopen`).
- Q3: `TurnDriver::run` has one turn in flight; the next `start()` runs only after `completed()`,
  and `TurnLog::sequence` advances only in `completed()` (turn_log.h:254-262), so `durable()`
  never resumes on a staged record and no write is issued before the previous barrier returns:
  no ordering the synchronous run lacks. The blocking entry from a loop's worker is refused while
  `run()` holds the driver (driver_test.cc:283), so no deadlock; with no deadline it throws
  `stalled` at once.
- Q4: task:95798a0 descends from task:18eb516 and is on task main; completion:7731315 is on
  completion main. A consumer that declares task before journal must pin task:95798a0 or later
  (`blocking_scheduler`), which consensus-driver's and Xapiand's repins have to carry.
- Q5: nothing in journal reads the old flat names; consensus `include/consensus/worker.h:166-181`
  calls `layout()`, `migrate` and `finish_migration`, which fail to compile at its repin
  (consensus-driver, as recorded in Detent:47117861).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | defect | A known group whose acknowledged records a damaged log no longer holds, and that no surviving manifest names, is offered for `create`: `Host::status` returns `none` for an open table entry absent from the log (host.h:272-280), and `open()` marks only `recovered().lost` (host.h:436-438). `create_group` then removes the group's artifacts and starts incarnation 2, so the loss is silent. TurnRecovery's own contract says a group the host knows that is in neither map is lost when the log is damaged (turn_log.h:62-64). The old marker made this case fail closed (Corruption). | 261116-140207-14fdf6 `absent` modes 1 and 2 | In `open()`, when a loop's recovery is damaged, mark that loop's open entries absent from `recovered().groups` lost with the rest, durably. Test: the probe's `absent` case in `placement_test`. |
| F2 | yes | defect | `close_group` makes the log forget the group before the table write (host.h:323-325). If the shard write fails (ENOSPC, EIO), the host keeps serving, retention recycles the group's records, and the table still holds it open with no record. After a restart, `status` is `none` and `create` removes its artifacts. | 261116-140554-3cc85e `close_failure` | Commit the closed entry first, from a copy, and forget only after the commit succeeds; on failure leave the entry and the log as they were. Test: `close_failure` in `host_test`. |
| F3 | yes | defect | Recovering a once-closed group a second time in one host process (a new Store after a fenced one, the remedy `healthy()` names) takes the table branch of `recover_shared`, because the first recovery released the log's records (`!turns_->has_group`). It returns the table's stale checkpoint as the frontier and stages `base + 1` behind the log's sequence. The next turn then fails the whole loop's driver. Before this quest, that case was Corruption for the one group. | 261116-140554-3cc85e `recover_twice` | Take the table branch only when the log holds nothing of the group (`turns_->sequence(group_) == 0`), and keep Corruption otherwise; or have the host refuse a second attach. Test: `recover_twice` in `host_test`. |
| F4 | no | defect | Each table transition mutates the in-memory entry before `commit` (`create_group`, `reopen_group`, `close_group`, `next_artifact`'s `high += 64`, `mark_lost`), so a failed table write leaves memory ahead of disk. `next_artifact` then hands out serials from a block that was never made durable (host.h:330-334). After a crash, a published artifact can lie outside `[mark, high)`, where the census and reclamation miss it, and its serial is issued again (EEXIST, one fence). | host.h:307-335, read | Commit a copy and apply it on success, or fence the host on a failed table write. This also covers F2's state half. |
| F5 | no | nit | `mark` is a low-water mark: one long-lived artifact pins it, so every census and reclamation pass probes every serial issued since then, one stat per serial. | host.h:346-348, read | Measure a group with a pinned dependency over many checkpoints, or keep the live serials sparse. |
| F6 | no | defect | When the waiter's scheduler declares no `post_call`, `Cancel` runs `hop` on the cancelling thread (turn_driver.h `Wait::Cancel`), and its `unlink` then races the loop's list. Every task scheduler declares `post_call`; a host scheduler might not. | turn_driver.h, read; task:95798a0 task.hh:221-224 | Refuse a scheduler without `post_call` at `await_suspend` instead of falling back. |
| F7 | no | gap | A cross-thread cancel that claims a waiter just before a stalled entry destroys its root leaves a queued `hop` that points into the freed frame. The one-argument `run` drops it with its scheduler, but a caller-owned scheduler (R5 overload) that is driven again runs it. | blocking.h `run`, task.hh:470-475, read; not probed | Document that a stalled entry's scheduler is not driven again, or drain claimed hops before destroying the root. |
| F8 | no | nit | `Host::open` writes a fresh `layout` over a directory that still holds old-layout names (`turns-*`, `owner.lock`, per-group directories) and ignores them (host.h:410-414). This is acceptable under D43; refusing unknown names is one line. The owner's call, keeping it is fine. | host.h:410-414, read | Optional: refuse the directory if any non-layout name is present at first open. |

## Not run

- The suite, ASan, TSan and mutant reruns at 535e9e5: still queued when I wrote this, so the
  A1/A2 crash-point counts (967 per mode) and the TSan cancel cases are the contributor's evidence,
  not mine.
- No real-filesystem (PosixIO) crash run; durability conclusions are from MemoryIO's crash model
  (unsynced names and data dropped at power loss).
- F7 was not probed; F4-F6 were read, not run.
- No performance measurement of block reservations or the `[mark, high)` probes.
