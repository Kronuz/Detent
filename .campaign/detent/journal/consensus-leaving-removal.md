# consensus-leaving-removal: journal

State: waiting on jobs 261121-014433-59d3fa 261121-014435-ea9ba4 261121-014436-34f470, CI 44891006605 (822aa7c)
Quest: .campaign/detent/quests/consensus-leaving-removal.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Correctness review APPROVED at consensus:7e83a6b. Head consensus:822aa7c folds F1's oracles into
driver_replace_test (test only).
Passed at 7e83a6b: A1 261121-012226-2fe3f5 (3000 seeds); A2 261121-012216-933a9e
(removal-from-any-drive), 261121-012218-e3e272 (removal-tick-unplanned), from driver_mutants.py;
suite subset 261121-011657-336f3f (27f79b0); A3 probe 261121-012106-ce07f5 at consensus:20198fc
(b8de3b8 + 9a2476d): replace, replicas, waits, defer, drain seed 1 all pass.
Waiting at 822aa7c: A1 261121-014433-59d3fa, A2 261121-014435-ea9ba4 (any-drive),
261121-014436-34f470 (tick-unplanned); CI 44891006605 (A4; 44889262133 on 7e83a6b in progress).

## Next

1. Read the jobs at 822aa7c and CI 44891006605; fill Evidence and Result.
2. When consensus-quiescence lands on main, merge main and rerun A3 at the quest head.

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
| A1 | 261121-031008-0547aa |
| A2 | 261121-031008-0547aa; fails without: 261121-031503-0effba, 261121-031505-02d29c |
| A3 | pending: rerun at head once consensus-quiescence lands (probe at consensus:20198fc passed: 261121-012106-ce07f5) |
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
- The removal now waits for the leader's next Tick after the commit: at most a heartbeat
  (next_deadline is the heartbeat for a leader with peers; a replacement always has one).
