# consensus-quiescence: journal

State: A1 Driver step done at consensus:b8de3b8 (pushed); waiting on jobs 261120-095339-49e5f9 (suite), 261120-095359-890b19 (mutant), 261120-095401-ec64ca; next A3
Quest: .campaign/detent/quests/consensus-quiescence.md (the lead's; never edit it)

## Doing

A3: TSan job over the simulation's closes and reopens.

## Next

1. A3: TSan simulation job over closes, reopens and quiescence under partitions.
2. A6: per-idle-group heap breakdown (consensus row + journal) against Xapiand's 6,984 B.
3. A2: `Wake`, `LeaderGone` in types.h; leader at rest stops heartbeats and CheckQuorum, followers
   stop timers; merge quest/detent/consensus-transport, then the per-node-pair detector raising
   LeaderGone for every group on the pair; mutant dropping one group's LeaderGone.
4. A4: idle CPU per group at 1M groups, strict job, results doc.
5. Crash at every step of close and reopen through the Driver (A5's last test).

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | Core: consensus:b4314e4, simulation_test `closing` cases (lone voter 2 barriers vs 5 without the record; three voters no election; stale record elects; bytes). Driver: consensus:b8de3b8, driver_close_test `reopen_barriers` (a request issued with the reopening recover: 2 turn barriers = an open group's; without the record 5; damaged bytes Corruption), job 261120-095401-ec64ca; mutant without the admission fix (journal refuses the batch) 261120-095359-890b19 --fails-without; suite 261120-095339-49e5f9 |
| A2 | <no evidence yet> |
| A3 | Core closes/reopens under the random simulation's partitions: 300 seeds x 10,000 steps, 31,198 closes, 16,401 resumed terms, 315 stale refusals, oracles clean (local run); TSan job pending |
| A4 | <no evidence yet> |
| A5 | `docs/mechanisms/quiescence.md` (consensus:4c0ad67, revised in b4314e4): RFTCLOS1 bytes, refusals, close and reopen rules; tests in simulation_test `record_bytes`, `lone_voter`, `three_voters` |
| A6 | <no evidence yet> |

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
- R6 Term record: the first storage batch after a reopen carries the hard state (Core::persist),
  so the reopen adds no barrier. Mutant without it fails `lone_voter` (local).
