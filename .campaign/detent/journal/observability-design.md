# observability-design: journal

State: waiting on jobs 261109-103954-f7533a 261109-104006-59599d 261109-104008-56988f 261109-104009-46152e
Quest: .campaign/detent/quests/observability-design.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State, the first step under Next, and the Evidence table. State is one of: working,
waiting on jobs <ids>, ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Writing `docs/design/observability.md` while the benchmark jobs run. Bench: `docs/design/observability_bench.cc`,
built against reader S6 (`Detent:b1d281c`, ref `s6`) on slot `obs-bench` by `docs/design/observability-bench.sh`;
summary by `docs/design/observability-summary.py`. Jobs (chained): build 261109-103954-f7533a (light),
smoke 261109-104006-59599d (strict), screen 261109-104008-56988f (strict), confirm 261109-104009-46152e
(exclusive), all at `15b6978`.

## Next

1. Read the confirm job's `confirm.md`; put its numbers in D2 of the design; choose the counting shape by them.
2. Commit the design, then rerun the confirm job at the head (`campaign job rerun 261109-104009-46152e`; the doc
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
