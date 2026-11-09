# namespace-s1-s2: journal

State: waiting on jobs 261108-212823-edd6f7 261108-212824-1e1d46 261108-212825-a55bba
Quest: .campaign/detent/quests/namespace-s1-s2.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State, the first step under Next, and the Evidence table. State is one of: working,
waiting on jobs <ids>, ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Merged main (Detent:09472b7), pins taken (Detent:c2025c2: journal:e5f6ff1, consensus:ab05507; check_pins 0 mismatches, gate pins job 261108-212143-7ae4f5 done). Job scripts committed in docs/measurements/sync-groups-20261108/ (mutant.sh, build.sh, arm.sh, summarize.py). Gate jobs native/asan/tsan queued at c2025c2 (261108-212143-{1824ae,df3187,69e836}) as early signal only: any non-.campaign commit changes the code key, so every row is rerun at the final head. Submitted: build slots s1s2-before (327ca58) and s1s2-after, mutant positive run.

## Next

1. Mutant jobs (one per name in mutant.sh; each must fail), A6 harness job, A9 arms (10K/100K x3 before/after, 5M x1), A8 reverts.
2. Write docs/mechanisms/sync-groups.md and docs/perf/sync-groups.md from job results.
3. Rerun `campaign verify` and the evidence jobs at the final head; fill the Evidence table; State ready for review.

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

<Decisions inside the quest's scope, so the reviewer can check them.>
