# namespace-measure: journal

State: working
Quest: .campaign/detent/quests/namespace-measure.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State, the first step under Next, and the Evidence table. State is one of: working,
waiting on jobs <ids>, ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Rules committed (A1). Harnesses committed (T9 driver, sync runner, flush counter, model `wire` mode). Jobs queued; T4(7) confirm jobs to submit once the smokes give per-size times.

## Next

1. T9 harness `docs/perf/namespace-measure-t9.cc`: corpus-5m.bin -> snapshot frames -> agent store (full copy) and delta frames f = 0.05/0.25/0.5/1 (5% tombstones) -> published; smoke at 100K.
2. T4(7): completion_sync_probe at completion:ab0f551, 64-4,096 files of 1 KiB, APFS exclusive and VM (strata's VM, port 18772) under QUIET_VM; flush counter `docs/perf/namespace-measure-flush.cc`.
3. A4: model `wire` mode at periods 16 and 64.
4. Verdicts by the Rules.

## Blocked on

Nothing.

## Evidence

One row per criterion, with what its kind needs: a job id (it, or the same job rerun, passed at
the current code), a `path`, a decision id, or for a [manual] one who checked what. Name every job
you examined, failures included, and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | `docs/perf/namespace-measure.md`: "## Rules" (committed before any job) |
| A2 | build 261110-134434-d53821; prepare 261110-134435-f6bf5e; smoke 100K 261110-134435-cffae5; confirm 5M rounds 1-5: 261110-134436-aaa766, 261110-134438-a127a8, 261110-134439-5fe56b, 261110-134440-c0d7f7, 261110-134441-b1e31d, 261110-134442-60b092, 261110-134443-f9ca66, 261110-134444-7a3ade, 261110-134446-f1bed4, 261110-134447-cfa18b |
| A3 | build 261110-132140-883986; smokes 261110-132141-33bbc2 (APFS), 261110-132141-b4d4a3 (VM); confirm pending |
| A4 | wire jobs 261110-132004-7e9617 (moderate), 261110-132005-0cd390 (hot-heavy), 261110-132006-e840b1 (keyset-0.30) |
| A5 | pending |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

1. Harness sources live in `docs/perf/namespace-measure-*.cc` (the quest's file set; `benchmarks/` is outside it), built in the job against the Detent build.
2. T4(7) adds 64 and 128 files to the design's 256/1,024/4,096: the compared sync groups hold 67-176 files.
3. The Linux VM is strata's local QEMU VM (127.0.0.1:18772, ext4 on virtio); no other Linux host.
