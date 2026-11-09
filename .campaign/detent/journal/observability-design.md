# observability-design: journal

State: waiting on jobs 261109-104451-e928ee 261109-104452-5b4e92 261109-104453-f581b5 261109-104454-a6435c
Quest: .campaign/detent/quests/observability-design.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State, the first step under Next, and the Evidence table. State is one of: working,
waiting on jobs <ids>, ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Design drafted in `docs/design/observability.md` (D1-D9, Q1-Q6, build plan OB1-OB6); its D2 cost table,
abstract result sentence and job ids are placeholders (NUMBERS, RESULT SENTENCE, COST TABLE, JOBID) until the
confirm job lands. Jobs at `fce36ed` (chained, slot `obs-bench`, ref `s6=b1d281c`): build
261109-104451-e928ee (light), smoke 261109-104452-5b4e92 (strict), screen 261109-104453-f581b5 (strict),
confirm 261109-104454-a6435c (exclusive). First chain at `15b6978` failed to compile (build
261109-103954-f7533a: the pinned metrics names `AtomicCounter` as an alias, so `~AtomicCounter()` failed);
its smoke, screen and confirm were skipped.

## Next

1. Read the confirm job's `confirm.md` (`$JOB_OUT`, or its log); put its numbers in D2 of the design; choose the counting shape by them.
2. Commit the design, then rerun the confirm job at the head (`campaign job rerun 261109-104454-a6435c`, with the build, smoke and screen; the doc
   commit changes job identity) and cite both.
3. Fill the Evidence table, Result; set State to "ready for review".

## Blocked on

Nothing.

## Evidence

One row per criterion, with what its kind needs: a job id (it, or the same job rerun, passed at
the current code), a `path`, a decision id, or for a [manual] one who checked what. Name every job
you examined, failures included, and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| <A1> | <job id at the current code> |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

<Decisions inside the quest's scope, so the reviewer can check them.>
