# delta-plan-warm: journal

State: waiting on jobs 261119-144618-cf06c9 261119-144628-7078b2 261119-144635-0b0e37 261119-144706-2f3ff1 261119-144708-b4e84e
Quest: .campaign/detent/quests/delta-plan-warm.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Harness committed (Detent:f02344d1): tests/detent_local/test_delta_plan_scaling_warm.{cc,sh,cmake} and
..._subcorpus.py. It plans one delta 3 times on one recovered generation (A cold, A again, B = seed+1;
BBA on even rounds), no publish. Chain queued: build 261119-144618-cf06c9, 500K corpus 261119-144628-7078b2,
prepare 5M 261119-144635-0b0e37, prepare 500K 261119-144706-2f3ff1, smoke 500K 261119-144708-b4e84e (strict).

## Next

1. Read the smoke (warm plans differ from cold? JSON per plan). Fix the harness if not.
2. Exclusive rounds (<=10 min each, `--for A1`): 500K arms 0.05, c0.05, 0.5, rounds 1-2; 5M the same.
3. Write the results into docs/perf/delta-plan-scaling.md ("A warm second plan"); fill the Evidence rows.

## Blocked on

<What, and who decides. "Nothing" when nothing.>

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

<Decisions inside the quest's scope, so the reviewer can check them.>
