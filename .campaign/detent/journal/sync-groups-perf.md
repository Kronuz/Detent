# sync-groups-perf: journal

State: ready for review
Quest: .campaign/detent/quests/sync-groups-perf.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State, the first step under Next, and the Evidence table. State is one of: working,
waiting on jobs <ids>, ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Done: `docs/perf/sync-groups.md` written from the 14 namespace-s1-s2 A9 exclusive jobs (all at Detent:35296ba, exit 0). Awaiting the lead's diff read.

## Next

1. Lead reads the diff of `docs/perf/sync-groups.md`.

## Blocked on

Nothing.

## Evidence

One row per criterion, with what its kind needs: a job id (it, or the same job rerun, passed at
the current code), a `path`, a decision id, or for a [manual] one who checked what. Name every job
you examined, failures included, and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | `docs/perf/sync-groups.md`; source jobs (exclusive, Detent:35296ba, exit 0): 10K before 261109-125239-019c9c 261109-121537-aa665e 261109-121537-59e880, after 261109-125239-e9ce13 261109-121537-ef207f 261109-125238-294dec; 100K before 261109-125239-90fffe 261109-125238-d9860a 261109-125239-2750d0, after 261109-125238-e2ca32 261109-125239-826187 261109-121537-2817a2; 5M before 261109-125238-dffa9c, after 261109-125239-dd7342 |

## Result

`docs/perf/sync-groups.md` (docs only). Cold bootstrap median before -> after: 10K 2.05 -> 1.42 s (-31%), 100K 12.70 -> 9.54 s (-25%), 5M 619.5 -> 463.2 s (-25%, one run per arm, no A/A). Restart: within noise at 10K (0.067 vs 0.069 s) and 100K (0.071 vs 0.070 s); 5M 1.354 -> 0.446 s is indicative only (one run per arm, within-run range as large as the gain). First restart is about 0.02 s faster at 10K and 100K. Leaves open: a second 5M run per arm; why the first restart differs.

## Rulings I made

1. The file already held a DRAFT with fixture counts, the ext4 screen and a blocked-qualification section. I kept the fixture counts and the ext4 screen (still-true measurements) and dropped the qualification and surviving-mutant narrative (superseded by the landed code; the mechanism doc is CURRENT).
2. Cold bootstrap is the harness's `agent_selected` mark; restart is the median of three agent restarts per run.
