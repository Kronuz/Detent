# task-settle-notes: journal

State: waiting on jobs 261119-144904-ce9830 261119-144917-4bc31f
Quest: .campaign/detent/quests/task-settle-notes.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

task:3df9339 (branch quest/detent/task-settle-notes) adds `test_settle_inline` to test/sync_test.cc and the cancel-callback wait rule to docs/API.md. Jobs: suite 261119-144904-ce9830; mutant (settle without the `requester_ != this thread` clause) 261119-144917-4bc31f.

## Next

1. When both jobs pass, record the ids in Evidence and set State to ready for review.

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | jobs 261119-144904-ce9830 (suite), 261119-144917-4bc31f (--fails-without mutant) |
| A2 | artifact: docs/API.md, under `cancel_callback` (task:3df9339) |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

- The case lives in sync_test.cc (the quest's file set), not deadline_test.cc as F6 suggested. It drives the scenario through a parent source (parent.request -> the deadline's callback advances past the deadline), uses a watchdog thread so the self-wait mutant fails instead of hanging.
- The mutant runs as a sed on task.hh inside the job's checkout, so no mutant commit exists.
