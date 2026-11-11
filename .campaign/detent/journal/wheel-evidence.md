# wheel-evidence: journal

State: waiting on jobs 261111-120056-0484f0 261111-120247-0a3582 261111-120249-898d56 261111-120250-120664
Quest: .campaign/detent/quests/wheel-evidence.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Rows queued at 67f3f4c3, stash pinned at stash:d249352: smoke 261111-120056-0484f0 (strict),
then single 261111-120247-0a3582, cross 261111-120249-898d56, mem 261111-120250-120664 (all
exclusive, --after the smoke). Tests passed: 261111-120050-51af98 (A1; ASan+UBSan and TSan).

## Next

1. Read the rows' summaries; rerun once with R=10 any row the rule calls inconclusive.
2. Results, numbers and the pick by the rule into `docs/design/wheel-evidence.md`.
3. Optional experiment 5 if (d) loses cross-thread only on inbox contention: a striped inbox.

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

<Decisions inside the quest's scope, so the reviewer can check them.>
