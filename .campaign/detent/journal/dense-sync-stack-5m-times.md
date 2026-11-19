# dense-sync-stack-5m-times: journal

State: waiting on jobs 261119-155051-8896b4 261119-155053-88d935 261119-155055-bede28 261119-155057-fc4275 261119-155100-6a6707 261119-155102-50abde 261119-155104-b28b3c 261119-155106-5540f0
Quest: .campaign/detent/quests/dense-sync-stack-5m-times.md (the lead's; never edit it)

## Doing

The first eight jobs (261119-1444xx) never ran: `--cwd` defaulted to the submitting directory `docs/measurements/...`, absent at the arm commits (FileNotFoundError). The script now cds to the repository root. Dry run `261119-155024-841b49` (none, sync, dense commit) showed cwd = slot root, `$JOB_REF_H` harness and the fixture present. Resubmitted 2026-11-19 15:50, exclusive, 10 min bound. Round 1: dense 8896b4, s2 88d935, stacked bede28, main fc4275. Round 2: dense 6a6707, s2 50abde, stacked b28b3c, main 5540f0. Commits: dense 9852a3c, s2 35296ba, stacked d04794d, main ea1fddc; harness ea1fddc. Old dbf54a was cancelled by me.

## Next

1. When the jobs finish, read each `SUMMARY` (agent_selected_s, restart_median_s, counters) from the job logs.
2. Replace the 5M "Times: not measured here" text in `docs/perf/dense-sync-stack.md` (it lives on `quest/detent/dense-sync-stack`; take its file at that branch's head) with a 5M times table beside the 100K one, and fill the Runs rows with job ids and commits. Name every arm's commit; state ratios with direction; main's times compare with nothing else (own fixture).
3. A1 evidence row: list the eight ids; the lead checks them against `campaign job list`.

## Blocked on

Nothing. The quest asks for a window agreed with the Xapiand lead; I have no channel to it and did not open `campaign window` (it holds other campaigns' light work). The lead may open one to start the queue.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | [manual] the eight resubmitted jobs above, all `exclusive` with a 10 min bound, quest-tagged; results pending |
| A2 | [artifact] `docs/perf/dense-sync-stack.md` pending |

## Result

Pending.

## Rulings I made

- No `--long`: it defers the job and the arms are well under the 10 min exclusive cap. Holds are at most 10 min, inside the 25 min limit.
- No `--for`: A1 is [manual] and campaign refuses `--for` on it; the jobs carry `--quest`.
- Smoke: main uses `10000-main`, the others `10000-r3`, as on 2026-11-18.
