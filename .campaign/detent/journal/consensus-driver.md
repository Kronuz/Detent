# consensus-driver: journal

State: fixing waiter_test cases (rows, close, close-read) after the step fix
Quest: .campaign/detent/quests/consensus-driver.md (the lead's; never edit it)
Code: /Users/kronuz/code/Kronuz/consensus-wt/detent/consensus-driver (branch quest/detent/consensus-driver)

## Contract: fixed

`consensus:driver`, written in consensus `docs/DRIVER.md` at consensus:6b3020c (the exact API,
the D8 table, the blocking entry, and the `try_submit` mapping). Summary for Xapiand group-core:

- `Driver<Sink>(Seams{scheduler_ref, completion_ref, journal::Host&, loop, seed}, Sink&)`, one per
  loop; `task<void> run(cancel_token stop)` is its one long-lived frame. The host runs the loop's
  `TurnDriver::run` (host.driver(loop)) beside it.
- Task calls by `GroupId` (the journal host's id): `create`, `recover(Restore)`,
  `propose(command) -> Placement{term,index}` at ProposalPlaced, `committed(Placement)`
  (`superseded`, `compacted`), `change_membership(next) -> Placement`, `transfer_leadership(target)`
  (`transfer_failed`), `repair(boundary) -> RepairCompleted`, `checkpoint(cap, Capture) ->
  CheckpointPublished` (Capture: `metadata()`, `read(span)`), `read() -> ReadReady`, `close()`.
  Every one takes an optional `cancel_token` except `close`.
- Plain on-loop calls (queue only, never step inline, so a Sink may call them): `open(GroupId,
  GroupOptions)`, `receive`, `applied`, `application_failed`, `activated`, `activation_failed`,
  `peer_limits`; on a closed group dropped and counted (`dropped`). `state(GroupId)` observes.
- Sink: `sink(GroupId, N&&)` for N in Send, Committed, RoleChanged, ActivateInstall,
  CheckpointPublished and RepairCompleted (only when no caller awaits), SnapshotNeeded,
  SnapshotTransmit, SnapshotReleased, Fenced.
- Errors: `closed`, `fenced`, `rejected{RejectReason}`, `superseded`, `compacted`,
  `transfer_failed`, `needs_peer`, `journal::stalled`, and `task::cancelled`.
- `consensus::run(Driver&, task::blocking_scheduler&, task<T>)`: D5's five steps.

Deviations from the design's D2 sketch (rulings R1-R4 below): no transport in `Seams` (the Sink
carries `Send`); `propose` takes the command and the Driver assigns request ids; `read` takes no
`Read`; `consensus::run` names its `blocking_scheduler`; `peer_limits` and `state` added.

## Doing

Job 261116-164245-3db927 (at consensus:0e8134a) failed far wider than its tail showed: driver_test
rows/checkpoints/close, blocking lone_voter/stalled/needs_peer/staged/ownership, waiter rows/close/
close-read, driver_idle (script cd). Causes fixed at consensus:8c32f07:
- a step stopped at a Tick-only turn (TurnResult::Idle) though the Tick left a persist or checkpoint
  preparation behind (lone voter's election waited for the next input; checkpoint stalled after
  chunk 1). New `Worker::runnable()`; Idle && runnable counts as progress.
- consensus::run's own stop left `stopping_` set: every later entry cancelled its calls, and a later
  `run()` parked in Idle with an empty token. The entry rearms the Driver when it returns.
- tests: blocking Sink applies its ranges; fence read waits on an unapplied range; threads case counts
  rounds as runs (the "0 runs"); scripts moved to test/ (tools/ is outside the file set).
Driver and blocking-entry cases all pass locally.
- consensus:b3b9580 waiter harness: a posted apply drained loop A inside its own step (cleared the
  outer `running`, stepped an empty queue: rows read SIGSEGV, ASan container-overflow). Fixed in
  the harness; runs end by applying, so placed commands reach the Sink before the commit check.
- consensus:18aa864 `waiter_test CASE LIMIT`; no-arg (ctest) run is 20,000 per case; checks script
  WAITER_LIMIT=1,000,000. Local Release: close-read exhaustive 916,524 in 191 s; close 2,000,000
  (limit) in 454 s, 0 failures. complete-resumes-before-unlink is killed by rows at 20,000 under ASan.

## Done

- consensus:ab82129 journal repinned to 7c08540; Worker on `Host&`; migration API gone.
- consensus:0acfaff driver.h, Core `next_deadline/term_at/leader`, lone voter due at Start.
- Standalone CMake makes task/completion available with Asio (journal turns it off).
- test/driver_test.cc (D8 rows on a lone voter), test/driver_simulation.cc (3 hosts through the
  Driver, run by simulation_test), test/waiter_test.cc (stateless explorer over the real Driver,
  threads case), test/blocking_entry_test.cc (D5 rules), bench/driver_idle.cc (Y6/Y9, a ctest),
  tools/driver_mutants.py (4 seeded faults, CONSENSUS_DRIVER_MUTANT).
- snapshot_channel.h D9: frame buffer allocated at the length prefix, freed after process().

## Jobs

- 261116-162515-04c287 waiter_test: rows 328 runs, teardown 187,185 runs, threads 300 rounds pass;
  close cases failed (retirement waited for a timer) -> fixed; rows propose pair failed (open).
- 261116-163455-2e4958 cancelled (superseded). 261116-164245-3db927 tools/driver_checks.sh: queued.
- Fixed since: an unopened group was stepped (run_one threw, fencing it); turn watches now end at
  shutdown; close runs retirement turns at once; a lone leader arms no heartbeat.

## Next

1. Green driver_test, waiter_test, blocking_entry_test, simulation, driver_idle.
2. Suite (`campaign verify`), ASan+UBSan and TSan builds of waiter/blocking/driver tests.
3. Mutant jobs (`--fails-without`, ASan) for the 4 driver mutants.
4. Idle-cost exclusive job; journal evidence rows; DRIVER.md/CHANGELOG.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | <no evidence yet> |
| A2 | <no evidence yet> |
| A3 | <no evidence yet> |
| A4 | <no evidence yet> |
| A5 | <no evidence yet> |

## Result

<When ready for review.>

## Rulings I made

- R1 `Seams` carries no transport: Q2 (a)'s Sink receives `Send`, so the transport (quest
  consensus-transport) is what the host's Sink forwards to and what calls `receive`. The Driver
  needs no transport seam, and the contract does not change when the transport lands.
- R2 The Driver assigns Core request ids (`propose(command)`, `read()`): a caller-chosen id can
  collide with another caller's on the same group; the id is a correlation token only.
- R3 `consensus::run` takes its `task::blocking_scheduler`: the entry drives a concrete scheduler,
  and the Driver must have been built over it (checked).
- R4 `committed()` below the retained log throws `compacted` rather than guessing: the entry's
  term is gone there, and a committed entry at that index can carry an earlier term.
- R5 A lone voter's election is due at `Start` (Core sets `election_deadline_ = now_`), and the
  Driver delivers a Tick right after Start, so it campaigns with no clock advance. admission.h is
  outside the file set, so Start itself does not campaign.
- R7 A witness member's Committed ranges never reach the Sink: the Driver acknowledges them
  (A4: a witness votes, never applies or holds state). Founding roles apply at `create` only.
- R8 "A lone voter beside a witness" (D5/R6) is not a valid membership (membership.h D9: a witness
  only in 2 + witness or a replacement window); `needs_peer` counts witnesses, tested on 2 + witness.
- R9 Idle cost: a lone voter arms no timer (a leader with no peers arms no heartbeat); a
  three-voter member's one wheel entry (election or heartbeat) is the design's per-group timer
  (idle-CPU table), so driver_idle allows at most one per member.
- R10 A Tick-only turn (TurnResult::Idle) counts as step progress only when `Worker::runnable()`
  (added, worker.h): otherwise an idle group would spin through its turn budget every step.
- R11 consensus::run rearms the Driver (stopping_, shutdown_) when it returns: its stop ends that
  entry only. A reactor `run()`'s shutdown stays final (calls after it complete `cancelled`).
- R12 tools/driver_checks.sh and tools/driver_mutants.py moved to test/ (tools/ is outside the file
  set); the checks script takes an absolute BUILD (the idle case's `cd` broke its output path).
- R6 A group with inputs or calls the Worker refuses for now and nothing in flight to free them
  re-arms its wheel entry 1 ms out (a bounded retry, not a spin).
