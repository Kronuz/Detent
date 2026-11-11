# namespace-measure: journal

State: waiting on jobs 261110-204105-976b50 261110-204113-306c02 261110-204114-8079b0
Quest: .campaign/detent/quests/namespace-measure.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State, the first step under Next, and the Evidence table. State is one of: working,
waiting on jobs <ids>, ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Waiting on jobs. Rules (A1) committed at `Detent:e4a1cc7` before any job. Harnesses: `docs/perf/namespace-measure-t9.{cc,cmake,sh}`, `-t9-fit.py`, `-sync.sh`, `-flush.cc`; model `wire` mode. A4's three wire jobs are done (exit 0). The 20 queued drains were cancelled by D55 and replaced by three jobs (below) at `Detent:67bac52`, which adds a time budget to both runners (skip a step, and say so, when elapsed + estimate would pass the budget; nothing timed changes).

## Next

1. When the three jobs finish: read each log's `skip` lines first; they list what did not run.
2. T9 (261110-204105-976b50, exclusive, 10 min, budget 540 s): one round, arms in priority order `full 0.5 1 0.25 0.05`, one run each. Estimate: full ~190 s, 0.5 ~170 s, 0.25 ~120 s, 1 ~220 s, 0.05 ~85 s (full measured 180 s apply in prepare; deltas scaled by the 100K smoke), 785 s in all, so about 3 arms run (expected full, 0.5, 0.25); 1 and 0.05 are cut. `python3 -I docs/perf/namespace-measure-t9-fit.py <log>`; the fit has n=1 per size and no f=1 point, so report R² and the interpolated α as a coarse interval. If the interval straddles a decision threshold (a rounding step of 1/8, or the 2C clamp), D55 allows one second run, with the arms that were cut; otherwise α by the rule. If α is not in {0.5, 1, 2}, submit `model wire SHAPE α` for the three shapes (light).
3. T4(7) APFS (261110-204113-306c02, exclusive, budget 540 s) and VM (261110-204114-8079b0, strict, `QUIET_VM`, budget 540 s): sizes in decision order. APFS `64:15 128:15 256:15 1024:3 4096:2`, estimated 26 + 50 + 100 + 80 + 210 = ~470 s at 26 ms per file-round; VM `64:15 128:15 256:15 1024:5 4096:3` plus build, ~60 + 15 + 30 + 60 + 80 + 190 = ~435 s at 16 ms. 4096 is the likeliest step the budget cuts. Interpolate the batched variant at 67-176 files from 64/128/256 (the only points the sync-bound rule reads); flush counts from the VM's `flush` lines.
4. Period verdict by the Rules (veto, sync-bound, efficiency); write "## Verdicts"; the design's numbers (outside the file set): report to the lead.
5. Note for the verdict: the probe's source writes 200 KB files whatever its fourth argument (its header and the log's "files of 200 KB" say so); the Rules text says 1 KiB. The `flush` driver does use 1 KiB. State both sizes in the verdict; do not edit the probe (A3: unchanged).
6. Optional: T9 in the VM (design asks for Linux; A2 does not).

## Blocked on

Nothing.

## Evidence

One row per criterion, with what its kind needs: a job id (it, or the same job rerun, passed at
the current code), a `path`, a decision id, or for a [manual] one who checked what. Name every job
you examined, failures included, and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | `docs/perf/namespace-measure.md`: "## Rules" (committed before any job) |
| A2 | build 261110-134434-d53821; prepare 261110-134435-f6bf5e; smoke 100K 261110-134435-cffae5; 5M round 261110-204105-976b50 (replaces the ten cancelled round jobs 261110-134436-aaa766 .. 261110-134447-cfa18b) |
| A3 | build 261110-132140-883986 (done); smokes 261110-132141-33bbc2 (APFS), 261110-132141-b4d4a3 (VM); APFS 261110-204113-306c02; VM 261110-204114-8079b0 (replace the ten cancelled part jobs 261110-134854-e31ee8 .. 261110-134912-536089) |
| A4 | wire jobs 261110-132004-7e9617 (moderate), 261110-132005-0cd390 (hot-heavy), 261110-132006-e840b1 (keyset-0.30), all exit 0 |
| A5 | pending |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

1. Harness sources live in `docs/perf/namespace-measure-*.cc` (the quest's file set; `benchmarks/` is outside it), built in the job against the Detent build.
2. T4(7) adds 64 and 128 files to the design's 256/1,024/4,096: the compared sync groups hold 67-176 files.
3. The Linux VM is strata's local QEMU VM (127.0.0.1:18772, ext4 on virtio, `cache=none`); no other Linux host. On this macOS host `quiet strict` acts as exclusive, so VM jobs run strict with `QUIET_VM=linux-vm` (pinning applies only on a Linux host).
4. 4,096 files run as three jobs of 3 rounds (9 in all) to stay under 30 minutes; the probe prints only per-job medians, so the 4,096 point is the median of the three medians.
5. T9's timed span ends at Published; the agent's later `confirm_recovered` is not timed (the rule). The prepared store is cloned (`cp -c`) before each delta run, so the base's pages are read cold.
6. T9 runs on the reference host only for now (A2); the design's Linux T9 is optional here.
7. D55 (short measurements, one strict or exclusive job of at most 10 min per row) replaces the ten pre-registered rounds and the T4(7) parts. Thresholds, rules text, the fit and the probe are unchanged; only the repetition shrinks: T9 is one round of one run per arm (rule 3 asks five per size), T4(7) 15 rounds at 64-256 files, 5 at 1,024 (rule: 15) and 2 and 3 at 4,096 on APFS and the VM (rule: 9). Rule 3's "median of five, spread = range" cannot be met; a difference is read with the interleaved pair's own noise, and a result whose interval straddles a threshold gets the one second run D55 allows. The 4,096-file part cut does not touch the sync-bound rule, which reads 64-256 files.
8. Time-box: each runner stops starting steps that would pass `T9_BUDGET_S` / `SYNC_BUDGET_S` (540 s of the 600 s bound), estimating from the slowest cost seen so far (1.15x), so a job ends clean instead of at its deadline (exit 124); tested with a stub binary. T9's arm order is full, 0.5, 1, 0.25, 0.05 because the crossing sits near 0.5-1 (100K smoke: delta 0.5 about equal to the copy).
