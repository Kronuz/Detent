# consensus-leaving-removal: journal

State: working
Quest: .campaign/detent/quests/consensus-leaving-removal.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Fix committed (consensus:9a2476d) on top of the Driver replacement test (consensus:170db35).
Jobs: 261121-010545-420472 (test at 170db35, main's core: expect the fence), 261121-010603-0d6903
(A1 at 9a2476d).

## Next

1. Read both jobs; fix the test harness if it does not build or does not complete.
2. A2: --fails-without at head with the `if (tick)` gate mutated away.
3. Suite (`campaign verify --for A4` / CI macos-arm64), push; A3 Xapiand repro pinned to head.

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
- The removal now waits for the leader's next Tick after the commit: at most a heartbeat
  (next_deadline is the heartbeat for a leader with peers; a replacement always has one).
