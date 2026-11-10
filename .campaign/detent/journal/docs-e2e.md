# docs-e2e: journal

State: working
Quest: .campaign/detent/quests/docs-e2e.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State, the first step under Next, and the Evidence table. State is one of: working,
waiting on jobs <ids>, ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Runner, checks, workflow, docs and 23 assertion blocks written; fixing the last e2e job failures, then the A2 and A3 jobs.

## Next

1. Fix assertions the e2e job shows wrong (docs wrong -> list in e2e_exceptions.txt, no page rewrites).
2. Mutant job (tests/docs/mutants.py) and overlap job (tests/docs/overlap.py).
3. Fill Evidence.

## Blocked on

<What, and who decides. "Nothing" when nothing.>

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

1. Runnable = fenced `sh` and `rest` blocks only; cpp/python/rust/json/text are display and not run (no compile harness here). Stated in docs/dev/docs-e2e.md.
2. Assertion form is `key: value` lines (stdout, stdout-matches, run, rewrite, json.PATH, ...), not Xapiand's JS pm.test: the runner is plain Python.
3. The runner rewrites the literal 8317 to the run's port; `rewrite:` patches what only a file can carry (the Quick-start's managed.json gets "port").
4. next#1 (kills the daemons) and welcome#1 (runs before the authority exists) are excepted; the health/CLI reference blocks that follow next run against the still-up daemons and the runner stops its own at the end.
5. Added docs/dev/docs-e2e.md (criterion A7) although `docs/dev/**` is not in the quest's file set; added tests/docs/link_exceptions.txt for two page defects the link check found (`/ #_top` splash, durable-relay `managed-delivery.md`), pages outside the file set.
6. Assertion blocks were added to 7 pages (quickstart, settings-and-flags, rollouts, group-overrides, reference/cli/*, health): outside the listed file set but scope item 2 of the assignment; they are hidden HTML comments.
