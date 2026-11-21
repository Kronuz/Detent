# consensus-leaving-removal: journal

State: waiting on jobs 261121-083440-bedfc4 261121-083442-2c1aec 261121-083445-9321a1 261121-083448-2a6b63 261121-083450-f4bdf7 261121-083453-9b1c92 and CI 44925311406 (all at 304bb1e)
Quest: .campaign/detent/quests/consensus-leaving-removal.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Correctness review APPROVED at consensus:7e83a6b, accepted at ebc3b2e (A1, A2, A4 met there).
Merged consensus main 5ebf834 (consensus-quiescence) at consensus:c285355: one conflict, Core::drive's
guard (main's `resting_`, this branch's `tick`); both kept. Build and 300 replace seeds pass.
A3 now runs in consensus (consensus:e92ab31, 304bb1e): driver_replace seed 26 has strata's
placement-repair replace seed 1 shape; mutant removal-unreserved (both removal mutants, the
pre-fix code) fences it as strata did. Test-only changes since the review: driver_replace_test
reports a fence with the world dump; driver_mutants.py takes a list of edits per mutant.
Jobs at 304bb1e: A1 261121-083440-bedfc4; A2 run 261121-083442-2c1aec, mutants
261121-083445-9321a1 (any-drive), 261121-083448-2a6b63 (tick-unplanned); A3 run
261121-083450-f4bdf7, mutant 261121-083453-9b1c92 (removal-unreserved); gate via verify; CI 44925311406.

## Next

1. Read the jobs at 304bb1e and CI 44925311406; fill Evidence.
2. When consensus-full-window-stall lands on main, merge main again and rerun the same jobs.

## Follow-ups (review, non-blocking)

- F1 (folded in, test only): driver_replace_test gains the reviewer's oracles from
  probe_replace.cc (job 261121-012750-1033ca, 20000 seeds): a client write outstanding
  throughout; after settling, the membership by value on every member, exactly one committed
  removal of three configurations, the removed member not leading, every committed write's value.
- F2: `removal()` lacks `can_change_membership()`'s `!transfer_` and `!witness_self()`; a leader
  mid-transfer or a witness leader before its handoff proposes the removal, as the old drive did.
  Intended (the removal completes across leader changes); D3's text could say so. No change.
- F3: a leader Tick with a removal due reserves two Control appends and can return Pressure,
  skipping that Tick's heartbeat and CheckQuorum; before, a leader Tick never could. Bounded (the
  promotion funded the same pack; idle packs are released). The CHANGELOG's "at most a
  heartbeat" holds when that Tick is admitted. No change.

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | pending at 304bb1e: 261121-083440-bedfc4 |
| A2 | pending at 304bb1e: 261121-083442-2c1aec; fails without: 261121-083445-9321a1, 261121-083448-2a6b63 |
| A3 | pending at 304bb1e: 261121-083450-f4bdf7 (driver_replace seed 26); fails without: 261121-083453-9b1c92 (removal-unreserved) |
| A4 | `ci:macos-arm64` |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

- Cause: Core::drive proposed D3's removal from every drive: a leader Tick (Worker plan count 0)
  and the Persisted after an AppendResponse-funded commit (one hard append, already consumed).
  Also any chain whose commit covers the promotion (ChangeMembership/Propose on a lone voter).
- Shape (option b): `Core::removal()` names the change; `drive(output, tick)` proposes it only
  when `tick` (an admitted Tick; the parameter was `allow_campaign`, same call sites). The Worker
  plans a leader Tick as `plan_event(ChangeMembership{0, removal})` (config append + commit
  hard, exactly the bytes propose_membership writes) or as nothing. One predicate, one place.
- A2's jobs pass only if the output names the fence (setup failures exit 126).
- A3 in consensus, not strata: strata's repro needs strata's harness (journal hosts per replica,
  faultfs, strata groups, the placement root), which consensus cannot depend on. A seed search on
  the pre-fix code (removal-unreserved; every seed 1-200 fences) for strata's shape (no witness,
  leaving voter not the leader, leader in term >= 2) found seed 26: node 2 leads term 2, fences on
  the removal of leaving voter 3, followers hold `1 2 3 4 leaving 3`; the same as strata's dump.
  Strata's own replace seed 1 passed on the fix: 261121-012106-ce07f5 (strata, not evidence here).
- Merge: a leader with a removal due never rests (leader_at_rest requires no leaving member), so
  quiescence leaves its Ticks, which propose the removal, running.
- The removal now waits for the leader's next Tick after the commit: at most a heartbeat
  (next_deadline is the heartbeat for a leader with peers; a replacement always has one).
