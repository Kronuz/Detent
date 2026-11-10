# namespace-measure: journal

State: waiting on jobs 261110-132141-33bbc2 261110-132141-b4d4a3 261110-134435-cffae5 261110-134435-f6bf5e and the T9/T4(7) confirm jobs below
Quest: .campaign/detent/quests/namespace-measure.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State, the first step under Next, and the Evidence table. State is one of: working,
waiting on jobs <ids>, ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Waiting on jobs. Rules (A1) committed at `Detent:e4a1cc7` before any job. Harnesses: `docs/perf/namespace-measure-t9.{cc,cmake,sh}`, `-t9-fit.py`, `-sync.sh`, `-flush.cc`; model `wire` mode. A4's three wire jobs are done (exit 0).

## Next

1. When T9 rounds finish: `python3 -I docs/perf/namespace-measure-t9-fit.py <the ten round logs>`; α by the rule. If α is not in {0.5, 1, 2}, submit `model wire SHAPE α` for the three shapes (light).
2. T4(7): combine the three 4,096-file parts (median of part medians; min/max across); interpolate the batched variant at 67-176 files; flush counts from the VM's `flush` lines.
3. Period verdict by the Rules (veto, sync-bound, efficiency); write "## Verdicts"; update the design's numbers if they move (design doc is outside the file set: report to the lead instead).
4. Optional: T9 in the VM (design asks for Linux; A2 does not).

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
| A3 | build 261110-132140-883986 (done); smokes 261110-132141-33bbc2 (APFS), 261110-132141-b4d4a3 (VM); APFS parts 261110-134854-e31ee8 (64/128/256), 261110-134858-dbd87a (1024), 261110-134901-832279, 261110-134905-524f3f, 261110-134909-b1785a (4096 x3 rounds each); VM parts 261110-134856-a431e8, 261110-134900-544c2b, 261110-134903-60e5bc, 261110-134907-5b1607, 261110-134912-536089 |
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
