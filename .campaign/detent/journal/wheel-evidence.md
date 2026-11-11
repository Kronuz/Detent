# wheel-evidence: journal

State: working
Quest: .campaign/detent/quests/wheel-evidence.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Writing the candidates (`docs/design/async-drivers-bench/wheel_vs_stash*`). The decision rule
(A3) is in `docs/design/wheel-evidence.md`, committed before any measured job.

## Next

1. Candidates (a)-(d) and the harness; smoke as a strict job.
2. Single-loop row (exclusive), cross-thread row (exclusive), memory.
3. Results and the pick by the rule into the note.

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
