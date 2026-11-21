# consensus-leaving-removal: journal

State: working
Quest: .campaign/detent/quests/consensus-leaving-removal.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Head consensus:27f79b0 (test 170db35, fix 9a2476d, 1000-seed ctest 06621ad, changelog 27f79b0),
pushed; CI run 44888389309 (06621ad) and the one for 27f79b0.
Reproduced on main's core: job 261121-010545-420472 (test at 170db35) fences at seed 1.
Local: 3000/3000 seeds pass at the fix in 11 s; both mutants fence at seed 1.
Waiting: A2 261121-011652-0bbc59 (core gate removed), 261121-011653-871e69 (leader Tick
unplanned), suite subset 261121-011657-336f3f, A3 261121-011227-d616f8 (consensus 06621ad: same
code as 27f79b0), info 261121-011236-08c6fc (all five strata group cases).

## Next

1. Read the jobs; CI green on 27f79b0 (A4); fill Evidence and Result; ready for review.

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
- A2's mutants are sed edits in a --fresh job's command, not test/driver_mutants.py entries
  (outside the file set); the job passes only if the output names the fence (else exit 126).
- ARCHITECTURE.md (outside the file set) still says "Idle leader ticks need no campaign
  pack": true, but a leader Tick with a due removal now reserves that change's pack; the lead
  may add the clause.
- The removal now waits for the leader's next Tick after the commit: at most a heartbeat
  (next_deadline is the heartbeat for a leader with peers; a replacement always has one).
