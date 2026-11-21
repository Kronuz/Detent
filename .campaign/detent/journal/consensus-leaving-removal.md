# consensus-leaving-removal: journal

State: waiting on jobs 261121-012216-933a9e 261121-012218-e3e272 261121-012226-2fe3f5 261121-011657-336f3f 261121-012106-ce07f5, CI on 7e83a6b
Quest: .campaign/detent/quests/consensus-leaving-removal.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Head consensus:27f79b0 (test 170db35, fix 9a2476d, 1000-seed ctest 06621ad, changelog 27f79b0),
pushed. Reproduced on main's core: 261121-010545-420472 (test at 170db35) fences at seed 1.
Passed: A1 261121-011140-7b0e14 (3000/3000 seeds); A2 261121-011652-0bbc59 (core gate
removed) and 261121-011653-871e69 (leader Tick unplanned): both fence.
A3 at consensus head cannot build: strata 30c3d26 pins consensus b8de3b8, which is on
consensus-quiescence, not main (Driver::recover's reopen argument). So A3 runs at
consensus:20198fc = b8de3b8 + cherry-pick of 9a2476d (local branch
probe/leaving-removal-on-b8de3b8, not pushed): job 261121-012106-ce07f5 (also prints the other
four group cases). Failed builds: 261121-011227-d616f8, 261121-011658-34495c.
Head now consensus:7e83a6b: mutants removal-from-any-drive and removal-tick-unplanned in
test/driver_mutants.py (CONSENSUS_DRIVER_MUTANT builds into consensus_driver_replace_test);
ARCHITECTURE.md states a leader Tick's removal pack (lead's ruling, file set widened).
Waiting: A2 from driver_mutants.py 261121-012216-933a9e, 261121-012218-e3e272; A1 at head
261121-012226-2fe3f5; suite subset 261121-011657-336f3f (27f79b0); A3 probe 261121-012106-ce07f5;
CI on 7e83a6b (A4).

## Next

1. Read 261121-011657-336f3f, 261121-012106-ce07f5, CI 44888809581; fill Evidence and Result.
2. When consensus-quiescence lands on main, merge main and rerun A3 at the quest head.

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| <A1> | <from `campaign brief`> |

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
