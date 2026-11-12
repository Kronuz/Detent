# stash-niche: journal

State: waiting on jobs 261111-194539-717932 261111-195159-e2d3d5
Quest: .campaign/detent/quests/stash-niche.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Rule committed (1986b696) before any job. Harness written (stash_niche.cc/.sh/_summary.py).
Job 261111-194539-717932 (light): test under ASan+UBSan and TSan, queued behind another quest's
exclusive hold; smoke job 261111-195159-e2d3d5 (strict, after it) queued. No measured row has run.

## Next

1. Smoke (strict) at small N to learn (f)'s drain time at 24 h / 1M keys; size the row jobs (<= 30 min).
2. Rows as exclusive jobs, per horizon x keys, 6 pairs at P = 1 4 14, tagged --for A1; mem rows (count build).
3. Fill the Results and verdict in docs/design/stash-niche.md by the rule.

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
