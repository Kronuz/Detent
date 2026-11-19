# delta-plan-warm: journal

State: waiting on jobs 261119-145559-4878c5 261119-145602-9c34e4 261119-145604-7698f5 261119-145607-de3e64 261119-145609-293d70
Quest: .campaign/detent/quests/delta-plan-warm.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

First chain failed: the runner required CORPUS even for `build` (261119-144618-cf06c9; dependents skipped).
Fixed in Detent:1fa84385 (CORPUS checked only where read; dry run with a cmake shim passes `build` without it).
Resubmitted at 1fa84385: build 261119-145559-4878c5, 500K corpus 261119-145602-9c34e4, prepare 5M
261119-145604-7698f5, prepare 500K 261119-145607-de3e64, smoke 261119-145609-293d70 (strict).

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
