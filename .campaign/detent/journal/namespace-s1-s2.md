# namespace-s1-s2: journal

State: waiting on jobs 261108-213700-dc3b49 261108-212824-1e1d46 261108-212825-a55bba 261108-213111-929a69 261108-213112-ecd616 261108-213114-e47da1 261108-213115-984fa5 261108-213116-edd4b1 261108-213713-d6e0a3 261108-213632-baa7ec
Quest: .campaign/detent/quests/namespace-s1-s2.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State, the first step under Next, and the Evidence table. State is one of: working,
waiting on jobs <ids>, ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Merged main (Detent:09472b7), pins taken (Detent:c2025c2: journal:e5f6ff1, consensus:ab05507; check_pins 0 mismatches, gate pins job 261108-212143-7ae4f5 done). Job scripts committed in docs/measurements/sync-groups-20261108/ (mutant.sh, build.sh, arm.sh, summarize.py). Any non-.campaign commit changes the job code key, so every row is rerun at the final head (the first gate jobs at c2025c2 were cancelled to free light slots; pins gate 261108-212143-7ae4f5 passed). Submitted: build slots s1s2-before (327ca58: 261108-213700-dc3b49) and s1s2-after (261108-212824-1e1d46); mutant positive 261108-212825-a55bba; mutants unsynced-descriptor 929a69, empty-payload-group ecd616, startup-before-barrier e47da1, closed-session 984fa5, phase-counts edd4b1 (each must FAIL; log shows 'MUTANT.. SURVIVED' if not; exit 125 = build error, not a kill); 10K smoke arms before (resubmitted; see below), after baa7ec (these test arm.sh itself; if they pass, queue 10K/100K x3 and 5M x1 with `--env ROUND=n`).

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

1. Job scripts live in docs/measurements/sync-groups-20261108/ (new dir, outside the quest's file set; no other quest writes it) because jobs run at a commit and need them there; the lead may add the dir to the file set. They reuse the unmodified 2026-11-07 harness (docs/measurements/sync-groups-20261107/coldboot.py, SHA-256 518db64e...75a5) and run its 10K smoke inside each arm job so its staged-run precondition holds.
2. A9 design: one arm per exclusive job; builds are light jobs on named slots s1s2-before (Detent:327ca58) and s1s2-after; arm.sh checks the slot's built commit and that the slot's code equals the job's. Before/after arms interleaved b1 a1 b2 a2 b3 a3; the A/A band is the spread among the before arms (10K, 100K); 5M one before, one after.

<Decisions inside the quest's scope, so the reviewer can check them.>
