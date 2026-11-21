# consensus-full-window-stall: journal

State: waiting on jobs 261120-201346-c3e8de 261120-201347-0f5364 261120-201349-112b82 261120-201350-63af72 261120-201357-bc0621 261120-201357-874d17 261120-201358-1847ef 261120-201358-da8c6d 261120-201359-7e393d
Quest: .campaign/detent/quests/consensus-full-window-stall.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Reorder "stall" (190522-1d0e1b) was the checker, not the pacer: the bench caps a large load at
128 MiB (bench.py load(): cap = (128 << 20) // size = 2,048 x 64 KiB). The paced leader committed
all 2,048 in 42.6 s (3.15 MB/s, p99 1,253 ms; main 1.82 MB/s, p99 2,784 ms, 1,721 commits in 60 s),
then the bench stopped proposing and sampling (diag span 42.6 s); windows 50-60 had nothing to
commit. Append trace (leader 3, diag status): paced appends averaged 213-794 KiB (median 576,
largest 1,152 KiB), 4-8 per 0.5 s, every 0.5 s sample advanced applied; no shrink to the floor,
no held-back appends. consensus:fe0d2dc ends the checker's windows at the load's last sample and
adds the netem table to docs/mechanisms/append-pacing.md. On the old outputs: fix loss PASS, fix
reorder PASS (6 windows, load ran 42.1 s), main loss FAIL, main reorder PASS.
Rerunning at fe0d2dc: A3 four strict VM jobs (checker from `git show fe0d2dc:`), A1 three, A4
TSan, msg-1 x10.

## Root cause (A2: docs/mechanisms/append-pacing.md)

1. core.h:1183 (at 8801b99): the batch bound is rpc_bytes; a Driver group's Core is built with
   Flow{} (worker.h:150), so an append carries up to 2 MiB. 32 x 64 KiB fill exactly one.
2. At ~0.5 MB/s (cubic under 1% loss/5% reorder, 40 ms RTT) that frame holds the Control
   connection ~4 s; heartbeats and votes queue behind it (send_message puts every Raft message on
   Control). With a flight out the leader sends no heartbeat anyway (core.h:1122).
3. Followers hear nothing past election_min and pre-vote (a follower missing the batch can win:
   the other grants); the leader's CheckQuorum (core.h:1077-1079) steps down at election_max.
4. core.h:1121 resends the whole batch every rpc_timeout while the first copy still crosses (the
   bench's 4 copies in 16 s); the copy's answer lands on a superseded flight and paces nothing.
5. Bench side: committed() of placements a new leader truncated waits until some term commits
   there, so the closed-loop load stops proposing and the cluster idles on heartbeats. The bench's
   diag records the leader only at load start, so the elections were invisible (msg-185 to xapiand).
31 in flight (1.94 MiB) stalls the same in the model: frame transit time, not a 2 MiB edge.

Fix: per-peer append pacing (Peer::frame, core.h pace()/frame_target()): start 64 KiB, double
on a fast full answer up to rpc_bytes, scale to the target (min(rpc_timeout, election_min)/2) on
a slow one, back to 64 KiB on a timeout.

## Next

1. Read jobs 261120-2013*: fill Evidence; Result.
2. Merge consensus main once consensus-quiescence lands (it also edits core.h).
3. Correctness review (high risk) once A1-A4 have evidence.

## Blocked on

Nothing. File set: bench/netlab/full_window_check.py (consensus:663a390, 3dc2f9e) is outside it;
the lead said they'll widen it.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | pending at fe0d2dc: 261120-201357-bc0621 (pass), 261120-201357-874d17 (--fails-without, main's core.h), 261120-201358-1847ef (--fails-without, no floor reset). At b08f834 (same core.h and test): 185055-ee51e1 passed; 185057-a51736, 185059-a31b7d failed as they should |
| A2 | `docs/mechanisms/append-pacing.md` (consensus:17751bc) |
| A3 | checker bench/netlab/full_window_check.py (consensus:fe0d2dc; windows end where the load does). Pending at fe0d2dc: fix 261120-201346-c3e8de (loss1%), 261120-201347-0f5364 (reorder5%); main --fails-without 261120-201349-112b82 (loss1%); main comparison 261120-201350-63af72 (reorder5%). At b08f834 core.h: fix loss PASS 190520-a407f0 (0.44 MB/s), fix reorder PASS under fe0d2dc's checker 190522-1d0e1b (2,048-commit cap in 42.6 s, 3.15 MB/s), main loss FAIL 190521-62807e, main reorder PASS 190523-86507a (1.82 MB/s) |
| A4 | pending at fe0d2dc: TSan 261120-201358-da8c6d (full_window_stall, pipeline, consensus, simulation, liveness); CI macos-arm64 at fe0d2dc |

Local runs (scratch, -O0, before campaign jobs): full_window_stall_test at 17751bc 9 checks 0
failures; with 8801b99's core.h 4 failures (slow path 4 elections, 2.3 commits/s against 6.7).

## Messages

- msg-1 (consensus_transport_peers late-append at -j2, loaded host): job 261120-201359-7e393d runs
  the transport suite at fe0d2dc, -j2, --repeat until-fail:10 (the failing job's set and -j). Note:
  pacing does not touch this case. Its commands are a few bytes, far below the 64 KiB frame floor,
  so pace() never shrinks them, and the whole-batch resend after rpc_timeout (core.h, Tick) is the
  same as main's. The timeout's floor reset only matters for frames above 64 KiB. A pass x10 here
  shows no regression, not a fix; the cause is the 40 ms compact rpc_timeout under host load
  (consensus-transport's).

## Result

<pending>

## Rulings I made

- A3 checker windows end at the load's last diag sample: a load that commits the bench's 128 MiB
  cap before 60 s has nothing left to commit, so later windows are not stalls. A load that does
  not answer still runs all windows to its seconds (main loss: 10 stalled). The VM jobs run the
  committed checker (`git show fe0d2dc:bench/netlab/full_window_check.py`), not the scratch copy.
- main reorder5% passes the bar (190523-86507a), so its rerun (261120-201350-63af72) is a plain
  comparison job, not --fails-without; A3's fails-without is the loss shape (201349-112b82).

- msg from lead (CMakeLists.txt joins the file set): consensus:b08f834 registers the test with four
  added lines after the simulation's, none modified, so it merges beside other quests' edits to the
  core test foreach.

- Root cause fixed in the Core's append sizing, not by changing Flow's default frame_bytes: a
  fixed 256 KiB cap (R19) would cost pipeline=1 Driver groups ~6x at 12 ms RTT
  (docs/perf/flow-control.md: 88.1 against 14.8 MB/s) and still stall below ~256 KB/s. Pacing
  keeps 2 MiB frames on fast paths (scratch model: 752 vs 770 commits at 10,000 B/ms).
- Target = min(rpc_timeout, election_min)/2: frames answer before a flight times out and before a
  follower campaigns; capped at 2^32 ms so bytes*target cannot overflow.
- A timeout drops the frame to the 64 KiB floor (slow start): the late answer to a superseded
  flight paces nothing, so without it a grown frame stays grown (test: 6.4 vs 15.8 commits/s).
- Pacing applies to any pipeline depth; under pipelining a flight's time includes queueing behind
  earlier ones, which only makes frames smaller (conservative).
- A3 (lead's bar): Xapiand's bench at e1144c2 (e12ee46 plus --large-inflight and an append
  trace, same core.h) with a job-local patch, ~/.scratch/detent/consensus-full-window-stall/a3/
  sustained.patch: --large-seconds (the large throughput load's duration) and the role changes
  seen during each load in its row. Not committed anywhere: the bench is netlab-consensus's.
  Sent to xapiand for adoption (msg-190). bench/netlab/full_window_check.py (consensus:3dc2f9e, a
  copy of a3/analyze.py with lint fixes) checks the bar from result-3.json and diag-3.jsonl (max applied over nodes at
  each 5 s edge from load start + 10 s) and exits 1 on a miss; the before jobs use it under
  --fails-without, after `test -s result-3.json || exit 127`. One strict 10-minute job per shape.
