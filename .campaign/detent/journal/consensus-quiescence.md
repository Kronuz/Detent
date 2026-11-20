# consensus-quiescence: journal

State: blocked on lead: file set (see Blocked on); waiting on jobs 261120-092714-977394
Quest: .campaign/detent/quests/consensus-quiescence.md (the lead's; never edit it)

## Doing

A1 Driver step: `Driver::recover` with a closed record, `Driver::closed_state(id)`, and a
barrier-counting Driver test (reopened lone voter's first write = an open group's). Needs the
files under Blocked on.

## Next

1. Once worker.h and admission.h are in the set: Worker forwarders `closed_state()` and
   `reopen(ClosedState)`; Propose and ChangeMembership plans reserve the hard state (the term
   record, 24 B); Driver `closed_state(id)` (RFTCLOS1 bound to the host table's incarnation) and
   `recover(id, restore, record)`; driver_close_test case counting turn barriers for the first
   write after a reopen against an open group's; crash at every step of close and reopen.
2. Quiescence proper (needs types.h): leader at rest stops heartbeats and CheckQuorum (R18),
   followers stop timers; `Wake`, `LeaderGone` messages (DTC2 tags in transport/codec.h).
3. A3: TSan simulation job over closes, reopens and quiescence under partitions.
4. A6: per-idle-group heap breakdown (consensus row + journal) against Xapiand's 6,984 B.
5. A2: merge quest/detent/consensus-transport, then the per-node-pair detector raising
   LeaderGone for every group on the pair; mutant dropping one group's LeaderGone.
6. A4: idle CPU per group at 1M groups, strict job, results doc.

## Blocked on

Lead: the file set lacks files the Driver step and quiescence must change. No in-flight quest
holds them (driver-pipeline is planned and already waits on core.h/driver.h):
- `include/consensus/worker.h`: two forwarders (`closed_state()`, `reopen()`), so the Driver
  reaches the Core it owns.
- `include/consensus/admission.h`: Propose and ChangeMembership plans reserve the hard state;
  without it the reopened leader's first batch (entry + term record) overruns its reservation
  and the journal refuses it (store.h:414, "oversized append reservation").
- `include/consensus/types.h`: `Wake` and `LeaderGone` (D12), which the quest's contract names.
- Recommendation: `campaign set consensus-quiescence files+=include/consensus/worker.h,include/consensus/admission.h,include/consensus/types.h,tools/mutants.sh,fuzz/**`
  (mutants.sh for A2's LeaderGone mutant; fuzz/ for the codec's new tags).
- Alternative: only worker.h + admission.h now (A1's Driver step, what Xapiand waits on), types.h
  when quiescence starts.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | Core: consensus:b4314e4, simulation_test `closing` cases (lone voter 2 barriers vs 5 without the record; three voters no election; stale record elects; bytes); suite 261120-092714-977394 (running). Driver step pending (Blocked on) |
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
- R6 Term record: the first storage batch after a reopen carries the hard state (Core::persist),
  so the reopen adds no barrier. Mutant without it fails `lone_voter` (local).
