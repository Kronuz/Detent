# stash-niche: journal

State: waiting on jobs 261111-200152-81602e
Quest: .campaign/detent/quests/stash-niche.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Rule committed (1986b696) before any job. First check 261111-194539-717932 failed: wheel-evidence's
stash levels refuse the last seconds of a 24 h horizon (past_horizon), a harness issue. The niche
arm now has its own levels (top level 4,801 slots), doc updated (coordinator's instruction; not a
rule change). Check 261111-200145-53e785 passed (ASan+UBSan, TSan). Smoke 261111-200152-81602e
queued. No measured row has run.

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

1. The niche stash arm uses a top level of 4,801 x 18 s (not 4,800): harness choice so 24 h fits from any base; wheel_vs_stash.cc untouched.

<Decisions inside the quest's scope, so the reviewer can check them.>
