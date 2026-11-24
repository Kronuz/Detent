# consensus-next-deadline-removal: journal

State: working
Quest: .campaign/detent/quests/consensus-next-deadline-removal.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Round 1 at consensus:a119a86 (quest branch). consensus:d31b547 = table + test with main's core.h
(the fails-without commit); consensus:a119a86 = the fix. Local: core_deadline_test 35 checks,
0 failures at a119a86; 8 failures at d31b547 (each omission). D4 mutant (early return restored)
fails its one check. Building the Core/driver tests locally (ASan, build/ in the worktree), then the
A3 probe (.campaign/detent/probes/consensus-next-deadline-removal/held.sh) locally.

## Next

1. Local: Core/driver tests pass; held.sh passes at a119a86 and fails at d31b547.
2. Queue once at the final head: A1 + --fails-without at d31b547, A3 held.sh + --fails-without at
   d31b547, campaign verify, push for CI. Record ids; State "ready for review".

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), name every job you examined, failures included (`campaign brief` lists failed
jobs no journal names), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| <A1> | <from `campaign brief`> |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

<Decisions inside the quest's scope, so the reviewer can check them.>
