# delta-plan-warm: journal

State: waiting on jobs 261119-153419-124e73 261119-153422-107aa3 261119-153424-6e0a4a 261119-153426-bc9994
Quest: .campaign/detent/quests/delta-plan-warm.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Chain at Detent:39155f00 passed (build 021ba5, prepare 500K fa3505, 5M eb759c, smoke acabbc). Smoke, 500K f 0.05
uniform, one process: plan A cold 2.012 s (CPU 1.724), A again on the same mapping 1.405 s (1.396), B (seed 1010)
1.547 s (1.512): warm same 30% less, other delta 23% less. Exclusive rounds submitted `--for A1` (10 min each):
500K rounds 1-5 (arms 0.05 c0.05 0.5) 261119-153419-124e73; 5M rounds 1, 2, 3 (same arms) 261119-153422-107aa3,
261119-153424-6e0a4a, 261119-153426-bc9994. Odd rounds plan AAB, even BBA.

## Next

1. Read the rounds (median per role per arm, ranges), write "A warm second plan" into
   docs/perf/delta-plan-scaling.md at the named commit, fill the Evidence rows, ready for review.

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
