# namespace-measure: journal

State: waiting on jobs 261110-204105-976b50 261110-204305-dd5888 261110-204307-3ae1e8 261110-204532-499625 261110-204533-185018
Quest: .campaign/detent/quests/namespace-measure.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State, the first step under Next, and the Evidence table. State is one of: working,
waiting on jobs <ids>, ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Waiting on jobs. T4(7) was re-pinned (ruling 9). Rules (A1) committed at `Detent:e4a1cc7` before any job. Harnesses: `docs/perf/namespace-measure-t9.{cc,cmake,sh}`, `-t9-fit.py`, `-sync.sh`, `-flush.cc`; model `wire` mode. A4's three wire jobs are done (exit 0). The 20 queued drains were cancelled by D55 and replaced by three jobs (below) at `Detent:67bac52`, which adds a time budget to both runners (skip a step, and say so, when elapsed + estimate would pass the budget; nothing timed changes).

## Next

1. When the three jobs finish: read each log's `skip` lines first; they list what did not run.
2. T9 (261110-204105-976b50, exclusive, 10 min, budget 540 s): one round, arms in priority order `full 0.5 1 0.25 0.05`, one run each. Estimate: full ~190 s, 0.5 ~170 s, 0.25 ~120 s, 1 ~220 s, 0.05 ~85 s (full measured 180 s apply in prepare; deltas scaled by the 100K smoke), 785 s in all, so about 3 arms run (expected full, 0.5, 0.25); 1 and 0.05 are cut. `python3 -I docs/perf/namespace-measure-t9-fit.py <log>`; the fit has n=1 per size and no f=1 point, so report R² and the interpolated α as a coarse interval. If the interval straddles a decision threshold (a rounding step of 1/8, or the 2C clamp), D55 allows one second run, with the arms that were cut; otherwise α by the rule. If α is not in {0.5, 1, 2}, submit `model wire SHAPE α` for the three shapes (light).
3. T4(7): build 261110-204305-dd5888 (light), then the size check 261110-204307-3ae1e8 (none): it must print `check ok` with every sampled file at 1024 bytes, and gates both measured jobs (`--after`). APFS 261110-204532-499625 (exclusive, budget 540 s): `64:15 128:9 256:7 1024:2 4096:1`, ~75 ms per file-round with the new per-file+directory variant, so 54 + 86 + 134 + 154 = ~430 s and 4096 is cut by the guard. VM 261110-204533-185018 (strict, `QUIET_VM`, budget 540 s): `64:15 128:15 256:15 1024:3 4096:1`, ~24 ms per file-round, ~60 build + 37 + 74 + 147 + 74 + 98 = ~490 s; 4096 is the likeliest cut. The cancelled first pair (261110-204113-306c02, 261110-204114-8079b0) ran the 200 KB probe and is void. Interpolate the batched variant at 67-176 files from 64/128/256 (the only points the sync-bound rule reads); flush counts from the VM's `flush` lines.
4. Period verdict by the Rules (veto, sync-bound, efficiency); write "## Verdicts"; the design's numbers (outside the file set): report to the lead.
5. The Rules text says completion:ab0f551; the probe is now completion:560c288 (ruling 9). Say so in the verdict; the batched variant is the same code.
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
| A3 | build 261110-204305-dd5888; size check 261110-204307-3ae1e8; APFS 261110-204532-499625; VM 261110-204533-185018 (replace the ten cancelled part jobs 261110-134854-e31ee8 .. 261110-134912-536089 and the void pair 261110-204113-306c02, 261110-204114-8079b0); the older build 261110-132140-883986 and smokes 261110-132141-33bbc2, 261110-132141-b4d4a3 ran ab0f551 and are superseded |
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
9. Cause: `bench/sync_probe.cc` at completion:ab0f551 has `constexpr kFileBytes = 200 * 1024` and ignores its fourth argument, so every probe run wrote 200 KB files, not the rules' 1 KiB (the log header said "files of 200 KB"). completion:560c288 (`Stage a new file for a group sync`) makes it `FILE_KB`, default 200, and adds a per-file sync + directory flush variant; its `SyncFiles` and batched path are unchanged (the commit's `include/` change is `image.h` only). I pin the probe at completion:560c288 rather than patch a copy; the extra variant makes each round about 3x dearer on APFS, so APFS rounds shrink (64:15 128:9 256:7 1024:2 4096:1) under D55. The `flush` driver always used 1 KiB. The `check` mode samples on-disk sizes while the probe runs; the build mode now starts from a clean directory because the source moved.
