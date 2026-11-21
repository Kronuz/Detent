# consensus-full-window-stall: journal

State: waiting on jobs 261120-185055-ee51e1 261120-185057-a51736 261120-185059-a31b7d 261120-183954-d2677d 261120-183957-d8625c
Quest: .campaign/detent/quests/consensus-full-window-stall.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Fix committed at consensus:17751bc (pushed). Waiting on A1 (pass + two --fails-without), A4 TSan,
A3 netem bench (one strict job per shape), and CI's macos-arm64 suite.

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

1. Read the jobs; fill Evidence; Result.
2. Merge consensus main once consensus-quiescence lands (it also edits core.h).
3. Correctness review (high risk) once A1-A4 have evidence.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | at b08f834 (registered ctest consensus_full_window_stall): 261120-185055-ee51e1 (pass), 261120-185057-a51736 (--fails-without, main's core.h), 261120-185059-a31b7d (--fails-without, no floor reset); earlier: 261120-183909-cb25cf passed at 17751bc (9 checks, 0 failures; slow path 0 elections 6.7 commits/s); pending: 261120-183911-97e441 (--fails-without, main's core.h), 261120-183913-e37593 (--fails-without, no timeout floor reset) |
| A2 | `docs/mechanisms/append-pacing.md` (consensus:17751bc) |
| A3 | pending: 261120-183954-d2677d (loss1%), 261120-183957-d8625c (reorder5%) |
| A4 | 261120-183915-9999fc passed (TSan: full_window_stall, pipeline, consensus, simulation, liveness at 17751bc; core.h unchanged since); CI macos-arm64 at 17751bc |

Local runs (scratch, -O0, before campaign jobs): full_window_stall_test at 17751bc 9 checks 0
failures; with 8801b99's core.h 4 failures (slow path 4 elections, 2.3 commits/s against 6.7).

## Result

<pending>

## Rulings I made

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
- A3 runs Xapiand's bench at e1144c2 (e12ee46 plus --large-inflight and an append trace, same
  core.h) with 17751bc's core.h, one strict job per shape. The bench's load is 2 s; "steadily for
  90 s" read as: the large_throughput rows answer (no "no answer in 92 s"), and diag's applied
  index advances through the load with no stall.
