# logger-slack-mutant: journal

State: waiting on jobs 261119-151549-b76537 (A1), 261119-151551-345463 (A2), 261119-151553-9aeb12 (50x loop)
Quest: .campaign/detent/quests/logger-slack-mutant.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Round 1 (logger:6b4fd39): a notify-counting test was timing-bound; mutant killed (45023b), suite failed (6f467c: 1 of 20 near notifies under load).
Round 2 (logger:5216469, pushed): `logger_detail::wake_deadline(ordered, due)` (logger.h/.cc) holds push_task's wake time; `test/deferred.cc slack` asserts it exactly, no threads. The slack mutant changes that function (`return due;`); `test/wake_mutant.sh`'s wake-slack-task mutant follows the call site.

## Next

1. On the jobs: A1 (mutants die), A2 (suite), 9aeb12 (slack test x50, a built loop).

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | job 261119-151549-b76537 (`sh test/deferred_mutant.sh`) |
| A2 | job 261119-151551-345463 (suite) |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

logger.h, logger.cc and test/wake_mutant.sh changed beyond the file set, on the coordinator's direction (expose the wake-deadline computation); wake_mutant.sh only follows the renamed call site. Test lives in deferred.cc, not wake.cc: bidirectional notify counts (near notifies, far never) so a too-wide slack is also caught.
