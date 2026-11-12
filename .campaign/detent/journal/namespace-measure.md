# namespace-measure: journal

State: waiting on jobs 261112-113923-c7fdbf 261112-113417-855a8e 261112-113952-04bb75 261112-113957-f378f9 261112-114001-1829af 261112-113801-4be23d
Quest: .campaign/detent/quests/namespace-measure.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State, the first step under Next, and the Evidence table. State is one of: working,
waiting on jobs <ids>, ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Resumed 2026-11-12. Read the logs: 261110-204532-499625 (APFS) and 261110-204533-185018 (VM) exit 0, `skip 4096:1` only on APFS (239 s + 353 s > 540 s); no skip in the VM log. T9 261110-204105-976b50 failed: arm full ran (57 s apply), then arm 0.5 died with `invalid paged local image` at 5M. Cause (lldb, delta on a clone of the prepared store): `PagedCursor::step` decoded page headers with default `paged::Limits` (records 500000), so a branch page whose key count passed 504,096 was refused; the 100K smoke never reached it. Main already has the fix (`paged_cursor.cc` uses `paged_format_limits()`), so it was merged (`Detent:4d457cb`, clean); the library is untouched. The prepared store predates the merge: rebuilt by the new chain below. Second defect found: the flush driver read stat index 14 (field 15, discard ticks), so every flush count read 0; fixed to field 16 (`Detent:82757c6`) and the VM job resubmitted.

Jobs at the merged code (the first prepare/round/smoke submit mangled its env under zsh and was cancelled; these are the resubmits): T9 build 261112-113417-855a8e; prepare 261112-113952-04bb75; 5M round 261112-113957-f378f9 (exclusive, budget 540 s, arms `full 0.5 1 0.25 0.05`; the full arm took 63 s in the failed run, so all five are expected to fit); smoke 100K 261112-114001-1829af; VM T4(7) with the flush fix 261112-113801-4be23d.

Pre-read of the rules on data already in hand (to be confirmed in "## Verdicts"):
- Sync-bound: batched at 71-176 files is 28-32 ms on APFS and 14-32 ms on ext4 (interpolated from 64/128/256), far under 250 ms on both: rule 2 does not fire.
- Wire (A4): veto does not fire at any alpha in {0.5, 1, 2} (worst sent/missing ratio 64:16 is at most 1.03; worst sent/copy stays under 1.00). Efficiency: hot-heavy mean read/missing is 1.336 at alpha 0.5, 1.257 at alpha 1 (both over 1.25: stay at 16) and 1.158 at alpha 2 (64 passes); moderate and keyset-0.30 pass at all three. So the period follows T9's alpha: 16 for alpha at most 1, 64 at alpha 2.

## Next

1. When the jobs finish: read each log's `skip` lines first. Fit T9: `python3 -I docs/perf/namespace-measure-t9-fit.py <round log>`; n=1 per size and no f=1 point if arm 1 is cut, so report R^2 and alpha as an interval; if the interval straddles a threshold, D55 allows one second run with the cut arms. If alpha is not in {0.5, 1, 2}, submit `model wire SHAPE alpha` (light) for the three shapes.
2. VM flush counts from 261112-113801-4be23d (`flush` lines), batched vs per-file; APFS by construction.
3. `campaign verify namespace-measure` for pins at the final commit; rerun any evidence job still flagged stale.
4. Write "## Verdicts" in `docs/perf/namespace-measure.md` (T9 alpha, T4(7) sync-bound, period, flush counts); the Rules text says completion:ab0f551 and flush field 15: say in the verdict that the probe is completion:560c288 (ruling 9) and the counter is field 16 (ruling 10). The design's numbers (outside the file set): report to the lead.

## Blocked on

Nothing.

## Evidence

One row per criterion, with what its kind needs: a job id (it, or the same job rerun, passed at
the current code), a `path`, a decision id, or for a [manual] one who checked what. Name every job
you examined, failures included, and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | `docs/perf/namespace-measure.md`: "## Rules" (committed before any job) |
| A2 | build 261112-113417-855a8e; prepare 261112-113952-04bb75; smoke 100K 261112-114001-1829af; 5M round 261112-113957-f378f9 (replaces the failed 261110-204105-976b50 (invalid paged local image at 5M, fixed on main) and the ten cancelled round jobs 261110-134436-aaa766 .. 261110-134447-cfa18b) |
| A3 | build 261110-204305-dd5888; size check 261110-204307-3ae1e8; APFS 261110-204532-499625; VM 261110-204533-185018 (replace the ten cancelled part jobs 261110-134854-e31ee8 .. 261110-134912-536089 and the void pair 261110-204113-306c02, 261110-204114-8079b0); VM rerun with the flush-counter fix 261112-113801-4be23d (its flush lines replace 261110-204533-185018's zeros); the older build 261110-132140-883986 and smokes 261110-132141-33bbc2, 261110-132141-b4d4a3 ran ab0f551 and are superseded |
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
10. The flush driver read `/sys/block/*/stat` index 14, which is field 15 (discard ticks), not field 16 (flush requests completed): every VM flush count in 261110-204533-185018 is 0 for that reason. The rules text says "field 15"; the intent is the completed-flush counter, so the driver reads field 16 and the VM job is rerun.
11. T9 failed on a library defect already fixed on main (default `paged::Limits` in `PagedCursor`), not a harness fault; merged main (never rebase) and rebuilt the prepared store instead of editing the library (outside the file set).
