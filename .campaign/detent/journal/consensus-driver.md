# consensus-driver: journal

State: working
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

Repin journal to journal:7c08540 and port the Worker's shared constructor to `journal::Host`.

## Next

1. Repin journal (7c08540: task 95798a0, completion 7731315 come with it); Worker shared ctor on
   `Host&`; drop migrate/layout/finish_migration (worker.h:166-181, R14); port
   shared_worker_test/shared_cluster_test to Host; suite job.
2. core.h: `next_deadline()`, `term_at()`, lone voter due at Start; `lone_voter` and recover tests.
3. driver.h: group table, waiters, step loop, wheel entries, Sink, every call; consensus::run.
4. Tests: driver simulation (inline_scheduler + HeldCompletion) with oracles; D8 rows; waiter
   interleavings + close/teardown with controls (ASan); real-thread close/cancel/teardown (TSan);
   blocking_entry rules; witness member.
5. snapshot_channel.h D9 (frame buffers on arrival); idle-cost bench (exclusive) for A5.

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
