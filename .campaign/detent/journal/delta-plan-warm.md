# delta-plan-warm: journal

State: waiting on jobs 261119-152531-021ba5 261119-152534-fa3505 261119-152536-eb759c 261119-152539-acabbc
Quest: .campaign/detent/quests/delta-plan-warm.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Second failure: prepare 7698f5/de3e64 exited 1 after a good full copy. Cause: the harness called the renamed
T9 main (`namespace_measure_t9_main`), which has no return on success, so its status was garbage. Fixed in
Detent:39155f00: the runner prepares with the real namespace_measure_t9 (the cmake file builds both) and an ERR
trap prints any failing command. Build 4878c5 and corpus 9c34e4 (500K) stand. Resubmitted at 39155f00: build
261119-152531-021ba5, prepare 500K 261119-152534-fa3505, prepare 5M 261119-152536-eb759c, smoke 500K strict
261119-152539-acabbc.

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
