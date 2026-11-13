# namespace-measure: journal

State: ready for review
Quest: .campaign/detent/quests/namespace-measure.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State, the first step under Next, and the Evidence table. State is one of: working,
waiting on jobs <ids>, ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Resumed 2026-11-12 (second session). Merged main (`a56bb57c`, has delta-plan-scaling `80af544a`) and ran the one budgeted 5M round: exclusive 261112-184757-fefadb (build 261112-184618-110b7d, prepare 261112-184754-b24467), all five arms ran, 170 s. Full 14.50 s (C 266 MB); delta 14.38 s (0.047 C), 26.16 s (0.233 C), 37.93 s (0.467 C), 53.34 s (0.934 C). 5M alpha by the rule 0.080 (1 Gb/s), 0.052 (10 Gb/s): inside the 100K per-run range 0.002-0.055, so it confirms; the 100K median sets the policy value, still the 1/8 floor. Where a delta stops paying: about 0.05-0.08 C; at C/8 it is 33% (10 Gb/s) or 18% (1 Gb/s) slower than the copy at 5M (19.3 s vs 14.5 s); the delta's fixed cost is 14.9 s at 5M against 0.37 s at 100K. Verdicts and design items 5-6 rewritten in `docs/perf/namespace-measure.md`. Everything else (T4(7), period 64) stands from the first session: alpha = 1/8, period 64.

## Next

1. Lead: review; apply "The design's numbers to change" (six items in the Verdicts) to `docs/design/namespace-delivery.md`, outside this quest's files; decide the 1/8 floor (the 5M crossing 0.052-0.080 is also under it; at the floor a block answer is slower than the copy at both sizes), and the period if the written-bytes premise matters. The 5M delta defect is fixed; the open cost is the delta plan's per-record cost (10-55 us vs 2.9 us).
2. Lead: A3's jobs ran without `--for A3` and a rerun of finished jobs was refused to me; tag them, or say to submit the 64-file smokes tagged A3 at the head (see Blocked on).

## Blocked on

A3 shows no evidence in `campaign brief` because the T4(7) jobs (APFS 261110-204532-499625, VM 261112-113801-4be23d, and the smokes/build 261112-132606-*) were submitted without `--for A3`. Options: 1. lead accepts them by id from this table (recommended: the measurements are valid, code unchanged in `docs/perf/` harness); 2. I submit the two 64-file smokes and the build tagged `--for A3` at the head (about 10 minutes, no new data); 3. rerun the two full jobs (exclusive 10 min + VM strict 10 min), the same numbers again.

## Evidence

One row per criterion, with what its kind needs: a job id (it, or the same job rerun, passed at
the current code), a `path`, a decision id, or for a [manual] one who checked what. Name every job
you examined, failures included, and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | `docs/perf/namespace-measure.md`: "## Rules" (committed before any job) |
| A2 | build 261112-115033-5cb640; prepare 261112-115039-e2a0e2; 100K runs 261112-115520-7fe532 (5 runs, replaces the one-round smoke 261112-115051-073a9c, cancelled); 5M round 261112-184757-fefadb (build 261112-184618-110b7d, prepare 261112-184754-b24467; replaces the failed 261112-115045-0c8be1 and 261110-204105-976b50 (invalid paged local image at 5M, fixed on main) and the ten cancelled round jobs 261110-134436-aaa766 .. 261110-134447-cfa18b) |
| A3 | verdict table in `docs/perf/namespace-measure.md` "T4(7)": build 261110-204305-dd5888; size check 261110-204307-3ae1e8; APFS 261110-204532-499625; VM 261110-204533-185018 (replace the ten cancelled part jobs 261110-134854-e31ee8 .. 261110-134912-536089 and the void pair 261110-204113-306c02, 261110-204114-8079b0); VM rerun with the flush-counter fix 261112-113801-4be23d (its flush lines replace 261110-204533-185018's zeros); the older build 261110-132140-883986 and smokes 261110-132141-33bbc2, 261110-132141-b4d4a3 ran ab0f551 and are superseded (rerun at 75faac0c: 261112-132606-9d21a6, 261112-132606-7ccdd0, 261112-132606-5cbd7d) |
| A4 | wire jobs 261110-132004-7e9617 (moderate), 261110-132005-0cd390 (hot-heavy), 261110-132006-e840b1 (keyset-0.30), alpha 0.5/1/2; and at T9's alpha 0.125: 261112-132124-1b4fd7, 261112-132129-82a3aa, 261112-132134-41fc3c, all exit 0 |
| A5 | `docs/perf/namespace-measure.md`: "## Verdicts" |

## Result

alpha = 1/8 (alpha_num 1, alpha_den 8); roll-up period 64. Written up with tables in `docs/perf/namespace-measure.md` "## Verdicts".

1. T9 (100K, five exclusive runs, 261112-115520-7fe532): full copy 0.340 s (C 4.998 MB); block answers 0.361 / 0.627 / 0.903 / 1.228 s at 0.048 / 0.236 / 0.470 / 0.939 C. Crossing 0.002-0.077 C per run, floor rule gives 1/8. At C/8 a block answer is 0.11-0.17 s (33-50%) slower than the copy. Relay encode CPU 0.003-0.036 s per answer; bytes read about 1.7-1.8 times sent. 5M (261112-184757-fefadb, one run per arm, merged main with the delta-plan fix): full 14.50 s (C 266 MB); delta 14.38 / 26.16 / 37.93 / 53.34 s at 0.047 / 0.233 / 0.467 / 0.934 C; alpha 0.080 (1 Gb/s) and 0.052 (10 Gb/s), inside the 100K range, so 100K sets the value and the 5M has no spread. A delta pays only below about 0.05-0.08 C; at C/8 it is 19.3 s against 14.5 s (+33% at 10 Gb/s, +18% at 1 Gb/s); it saves 8x the bytes there, which wins only on links under about 0.39 Gb/s. Delta fixed cost 14.9 s at 5M vs 0.37 s at 100K.
2. T4(7): batched 26 / 13 ms (64 files, APFS / ext4), 31 / 32 ms at 176 files; per-file 1,385 / 87 ms at 64. ext4: one device flush per file, batched or not; macOS one per group (by construction). Rule 2 does not fire.
3. Period: wire veto no; efficiency bands hold at alpha 1/8 (sent mean at most 1.033 of period 16, read mean at most 1.028): 64. Written bytes at 1/8: moderate +2.4%, hot-heavy -9.3%, key set -23.7% against the rule's premise of 22-25% fewer.
4. Open: the delta plan cost per record; the 1/8 floor sits above the crossing at both sizes; the design's numbers to change are six items in the Verdicts (alpha, period, model tables at alpha 1, T4(7), T9 row); the `layout` mode takes no alpha argument so its tables are not rerun.

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
10. The flush driver read `/sys/block/*/stat` index 14, which is field 15 (discard ticks), not field 16 (flush requests completed): every VM flush count in 261110-204533-185018 is 0 for that reason. The rules text says "field 15"; the intent is the completed-flush counter, so the driver reads field 16 and the VM job is rerun.
11. T9 failed on a library defect already fixed on main (default `paged::Limits` in `PagedCursor`), not a harness fault; merged main (never rebase) and rebuilt the prepared store instead of editing the library (outside the file set).
12. Main's one-knob-type change removed `paged::Definition::{boolean,default_int,default_bool}` and `Override::percentage`, and changed the wire definition to `[default, rollout_pct|nil, rollout_value|nil, [rows]]` with int64 forced overrides. The DTCORP01 corpora (dense, 4-5 Nov) hold the retired two-kind paged bodies, so the harness decodes those bodies itself (local legacy reader) and writes the one-knob wire definition: a bool is default 0/1, rollout value 1 when any percentage is present; an int keeps its default; `flip` toggles a bool's default or adds 1. Checked to build against the merged tree (incremental build of the slot's tree) and to run at 100K (full, then delta 0.5 on a clone: both published) before submitting; the x_raw bytes differ from the earlier runs because the wire form changed, so earlier T9 numbers are void anyway.
13. The runner reads $CORPUS even in build mode, so the build job carries the corpus env (the first resubmit, 261112-114942-c476b9, failed on that and its chain was skipped).
14. D85 (merged `Detent:8e0d84b`): A2 is five runs per arm on the 100K corpus (exclusive job 261112-115520-7fe532: prepare, then `round 1..5 full 0.05 0.25 0.5 1`) plus the one budgeted 5M round 261112-115045-0c8be1; no more 5M jobs. alpha comes from each 100K run (fit over its 4 sizes + full) and from the 5M round; inside the 100K range the 100K median sets the policy value, outside it the 5M round decides and the verdict says it has no spread. The Rules doc's repetition text and a "Rulings that changed the runs" section (D55, probe pin, field 16, D85) were amended before any new data; thresholds and decision rules are unchanged. The one-round 100K smoke is replaced by the five-run job.
15. Second session: jobs run in their own `src` checkout, so the 5M chain passes `T9_BUILD` and `T9_ROOT` (absolute, in the build slot) and a distinct env on the build so it reran at merged main (an identical earlier build was reused and its dependants skipped). Failed first submissions 261112-184510-803a38 (env passed as one word), 261112-184622-9a251f (build path) are not evidence.
