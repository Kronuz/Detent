# consensus-quiescence: journal

State: A1, A3 met; A6 done at consensus:95eed40 + ebd8632 (doc); A2 Core rest in progress (uncommitted)
Quest: .campaign/detent/quests/consensus-quiescence.md (the lead's; never edit it)

## Doing

A2 Core: Timing::quiesce; leader rests when every replica acked the log and commit (Peer::told),
follower rests on an append whose commit covers its log; Wake, LeaderGone{node} in types.h;
Driver::wake(id), Driver::leader_gone(node) via a per-row notices queue (PeerLimits, Wake,
LeaderGone); simulation: half the seeds quiesce with a node-pair detector oracle; quiescence
cases (at_rest, leader_gone, restarts); driver_test leader_gone. Debugging: leader never rests.

## Next

1. A2 transport: merge quest/detent/consensus-transport, HostTransport::on_node_gone(loop, fn)
   on a Control link's end, wired to Driver::leader_gone in groups_test; e2e case: leader node
   stops, every group it led elects within its timeout; mutant leader_gone skipping one group.
2. A4: idle CPU per group at 1M groups (quiesce vs per-group heartbeats), strict job, doc.
3. Crash at every step of close and reopen through the Driver (A5's last test).

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | Core: consensus:b4314e4, simulation_test `closing` cases (lone voter 2 barriers vs 5 without the record; three voters no election; stale record elects; bytes). Driver: consensus:b8de3b8, driver_close_test `reopen_barriers` (a request issued with the reopening recover: 2 turn barriers = an open group's; without the record 5; damaged bytes Corruption), job 261120-095401-ec64ca; mutant without the admission fix (journal refuses the batch) 261120-095359-890b19 --fails-without; suite 261120-095339-49e5f9 |
| A2 | <no evidence yet> |
| A3 | TSan job 261120-095452-8e93e6 at b8de3b8: simulation 300 seeds x 10,000 steps (31,198 closes at rest, 16,401 resumed terms, 315 stale refusals) and the Driver simulation, oracles clean; reopen_barriers. To rerun once quiescent seeds land |
| A4 | <no evidence yet> |
| A5 | `docs/mechanisms/quiescence.md` (consensus:4c0ad67, revised in b4314e4): RFTCLOS1 bytes, refusals, close and reopen rules; tests in simulation_test `record_bytes`, `lone_voter`, `three_voters` |
| A6 | docs/perf/idle-bytes.md (consensus:ebd8632): idle row 5,703 B (before 6,807 B; Xapiand 6,984 B), jobs 261120-101900-6202e7 (after), 261120-101850-c631af (before probe 7c14f40), owners 261120-101825-a635c0 |

## Result

<pending>

## Rulings I made

- R1 The closed record is stored by the host (strata's bundle, a parked group's table entry) and
  handed back at reopen; consensus writes nothing at close. No journal or checkpoint format
  change.
- R2 The leader is recorded, never inferred from a lone voter's own ballot: a three-voter
  leader X of T can remove the third voter and then itself, leaving a replica the only voter with
  its own ballot in T while X still leads T (two leaders in T; a conflicting entry fences it).
- R3 The record is used only on an exact match (hard state, last index and term, membership and
  its index); a stale one is ignored (the group elects), corrupted or foreign bytes throw.
  Safety needs only the term (one leader per term); the match keeps the record truthful.
  Measured: a mutant accepting any same-term record passes the random simulation (it is safe),
  and fails the deterministic `lone_voter` stale case.
- R4 The leader at close need not vote in the membership in force: a leader leads until its own
  removal commits (the random simulation found such a record within 300 seeds).
- R5 The closed state's types and codec live in core.h while storage.h and types.h are outside
  the file set; they move there if the set widens.
- R7 Driver::closed_state requires no waiter and an empty inbox besides Core::closed_state: the
  group is at rest. recover(id, restore, closed) decodes before any recovery IO, so a refused
  record leaves a half-opened row that closes (a Core built by recover and never started never
  drains).
- R8 Propose and ChangeMembership plans always reserve the hard state (not only after a reopen):
  one plan shape, and a reservation is released at the append.
- R9 The barrier count is the host turn log's `stats().barriers`. A request issued after the
  recover completes costs 3: the journal re-stages the table's checkpoint record as the group's
  first record (journal recover_shared) and it takes its own turn; issued with the recover, it
  rides that turn (2). Documented in quiescence.md "Through the Driver"; Xapiand issues the request
  with the recover.
- R10 A6: rare state boxed (capture, install, transfer, snapshot result, founding); pack_,
  persist_, pending_ stay inline (hot path, an allocation per proposal otherwise); config copies
  (~320 B) and the journal's 3,052 B reported, not cut. idle_bytes is report-only: block sizes
  differ by platform, a bound would flake on Linux.
- R11 A2: quiescence is opt-in (Timing::quiesce): a host without a detector keeps its timers.
  A follower at rest holds its leader current (refuses pre-votes) except to that leader itself;
  a leader at rest wakes on any frame, so a restarted member's pre-vote wakes it.
- R6 Term record: the first storage batch after a reopen carries the hard state (Core::persist),
  so the reopen adds no barrier. Mutant without it fails `lone_voter` (local).
