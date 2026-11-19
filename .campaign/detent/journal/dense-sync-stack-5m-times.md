# dense-sync-stack-5m-times: journal

State: waiting on jobs 261119-144427-b7e082 261119-144433-dbf54a 261119-144436-2969e1 261119-144438-9062a6 261119-144443-94dd46 261119-144450-841140 261119-144458-1b6a72 261119-144505-d05453
Quest: .campaign/detent/quests/dense-sync-stack-5m-times.md (the lead's; never edit it)

## Doing

Eight exclusive jobs queued 2026-11-19 14:44 by `docs/measurements/dense-sync-stack-20261118/submit-5m-times.sh ARM ROUND`: one arm per job, 10 min bound (a 5M arm took 205 to 359 s on 2026-11-18, so no `--long`). Round 1: dense b7e082, s2 dbf54a, stacked 2969e1, main 9062a6. Round 2: dense 94dd46, s2 841140, stacked 1b6a72, main d05453. Commits: dense 9852a3c, s2 35296ba, stacked d04794d, main ea1fddc (harness at ea1fddc for all; main reads `5000000-flat-main`, the rest `5000000-flat-r1`). Machine load was 11 at submit; exclusive waits for a lull or 20 min since the last drain.

## Next

1. When the jobs finish, read each `SUMMARY` (agent_selected_s, restart_median_s, counters) from the job logs.
2. Replace the 5M "Times: not measured here" text in `docs/perf/dense-sync-stack.md` (it lives on `quest/detent/dense-sync-stack`; take its file at that branch's head) with a 5M times table beside the 100K one, and fill the Runs rows with job ids and commits. Name every arm's commit; state ratios with direction; main's times compare with nothing else (own fixture).
3. A1 evidence row: list the eight ids; the lead checks them against `campaign job list`.

## Blocked on

Nothing. The quest asks for a window agreed with the Xapiand lead; I have no channel to it and did not open `campaign window` (it holds other campaigns' light work). The lead may open one to start the queue.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | [manual] jobs above, all `exclusive` with a 10 min bound, quest-tagged; results pending |
| A2 | [artifact] `docs/perf/dense-sync-stack.md` pending |

## Result

Pending.

## Rulings I made

- No `--long`: it defers the job and the arms are well under the 10 min exclusive cap. Holds are at most 10 min, inside the 25 min limit.
- No `--for`: A1 is [manual] and campaign refuses `--for` on it; the jobs carry `--quest`.
- Smoke: main uses `10000-main`, the others `10000-r3`, as on 2026-11-18.
