# turn-seal-forget-flake: journal

State: waiting on jobs 261118-140358-398c2a 261118-140437-ce3f9e 261118-140359-9147b4
Quest: .campaign/detent/quests/turn-seal-forget-flake.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Fix committed (journal:47c98b6) and pushed: forget_threads runs >=400 turns, then until group 1 is forgotten and group 2 sealed, 10 s deadline. Cause: sequence(2) stayed 0 (writer never staged group 2 in time); failed() cannot be it, begin()/completed() throw into the test rather than set it silently.

## Next

1. When jobs finish: A1 loop x200 (398c2a), mutant fails-without (860226), suite (9147b4); then CI; then ready for review.

## Blocked on

Nothing

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
