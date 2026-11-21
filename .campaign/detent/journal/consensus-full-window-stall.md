# consensus-full-window-stall: journal

State: waiting on jobs 261121-082754-676cb7 261121-082754-8c8b3b 261121-082754-ab5004 261121-082754-412a78 261121-082755-291adf (and CI run 44924678818)
Quest: .campaign/detent/quests/consensus-full-window-stall.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Merged consensus main (consensus-transport 9bbd632, consensus-quiescence 5ebf834) at
consensus:5a114dd; correctness review accepted at consensus:8e55060. Every evidence job reran at
5a114dd (State's ids); CI on the pushed branch pending. Lands first, before consensus-leaving-removal
and consensus-driver-install.

core.h merge: two conflicts, both independent additions beside each other. Peer keeps
quiescence's `told` (the commit an acknowledged append carried, read by the rest check) and
pacing's `frame`; a matched append answer runs `pace(peer, *at)` then
`peer.told = max(told, at->commit)`. Flight auto-merged with both `bytes` and `commit`; the
timeout's floor reset, the batch bound and `Peer{...}` emplaces (frame defaults to the floor)
merged clean. A resting leader sends nothing, so pacing keeps its frame across a rest. Local
build of every target at 5a114dd: clean.

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

1. Jobs at 5a114dd and CI green, then the lead lands.

## Blocked on

Nothing. File set: bench/netlab/full_window_check.py (consensus:663a390, 3dc2f9e) is outside it;
the lead said they'll widen it.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | at 5a114dd (merged main): 261121-082753-d21525, 261121-082753-a72cc7 (registered ctest); F1 reviewer's probe 261121-082753-627be6. Fails without (mutants, unchanged by the merge): 261120-183911-97e441, 261120-183913-e37593, 261120-185057-a51736, 261120-185059-a31b7d, 261120-201357-874d17, 261120-201358-1847ef, 261120-225642-33fe0e, 261120-225643-3b255d, 261120-225646-4a2941 |
| A2 | `docs/mechanisms/append-pacing.md` (consensus:17751bc; netem table consensus:fe0d2dc, both runs per build consensus:90ff878) |
| A3 | at 5a114dd: 261121-082754-5ae1d3, 261121-082754-676cb7, 261121-082754-20565a, 261121-082754-8c8b3b, 261121-082754-ab5004, 261121-082754-412a78, 261121-082755-291adf; fails without: 261120-190521-62807e, 261120-201349-112b82 (main's core.h) |
| A4 | at 5a114dd: TSan 261121-082753-887444; CI macos-arm64 on the pushed branch pending |

Earlier, at 8e55060 (all passed): A1 261121-011735-e4cb4a, 261121-011736-1b0bbb; A3
261121-011735-a92608, -aec8a3, -238091, -f02351, 261121-011736-935bae, -8b633e, -a2dfb0; A4
261121-011736-781f39. At f178d4e: F1 probe 261120-225650-745988.

Local runs (scratch, -O0, before campaign jobs): full_window_stall_test at 17751bc 9 checks 0
failures; with 8801b99's core.h 4 failures (slow path 4 elections, 2.3 commits/s against 6.7).

## Messages

- msg-1 (consensus_transport_peers late-append at -j2, loaded host): job 261120-201359-7e393d runs
  the transport suite at fe0d2dc, -j2, --repeat until-fail:10 (the failing job's set and -j):
  passed, 53 of 53 tests x10 in 591 s, late-append included. Note:
  pacing does not touch this case. Its commands are a few bytes, far below the 64 KiB frame floor,
  so pace() never shrinks them, and the whole-batch resend after rpc_timeout (core.h, Tick) is the
  same as main's. The timeout's floor reset only matters for frames above 64 KiB. A pass x10 here
  shows no regression, not a fix; the cause is the 40 ms compact rpc_timeout under host load
  (consensus-transport's).

## Result

Per-peer append pacing (consensus:17751bc, `Peer::frame`, `pace()`, `frame_target()` in
include/consensus/core.h) keeps a full 32 x 64 KiB window committing under 1% loss at 20 ms, where
main's leader loses its followers to elections and stalls (both main runs). Under 5% reorder both
builds commit every window; throughput is within run noise (main 1.82/3.62, paced 3.15/2.99
MB/s). Mechanism and numbers: docs/mechanisms/append-pacing.md (consensus:90ff878). Test:
test/full_window_stall_test.cc (ctest consensus_full_window_stall). Checker:
bench/netlab/full_window_check.py. The bench's sustained-load patch (a3/sustained.patch) is
netlab-consensus's to adopt (msg-190).

## Review findings (correctness, 90ff878)

- F1 (blocking): fixed, consensus:fb8931f. replicate()'s batch loop now breaks on
  `bytes >= rpc_bytes ||` (deliver()'s guard), so an oversized first entry goes alone. Test:
  full_window_stall_test large_commands(): 100 KiB commands at 600 B/ms (20 s) and 1 MiB at
  50,000 B/ms. Local: with the guard 0 elections, 4.8 commits/s (path 5.9), largest 100 KiB and
  2,048 KiB; without it 3 failures (3,200 KiB and 8,192 KiB appends, an election). Jobs:
  261120-225646-4a2941 (--fails-without, guard reverted), 261120-225642-48b0ca (pass),
  261120-225650-745988 (reviewer's probe.sh asis at head).
- F2: taken, consensus:f178d4e. A load that answers before its seconds passes only with the whole
  cap committed and no failures. On the existing outputs: fix loss/reorder PASS, main loss FAIL
  (whole=False too), main reorder PASS.
- F3: follow-up. The sustained-load bench patch belongs to xapiand's netlab-consensus (msg-190
  asks them to adopt it); until then A3 reruns need ~/.scratch/detent/consensus-full-window-stall/
  a3/sustained.patch kept (no sweep before this quest lands).
- F4: no change. The test models TCP's result of drops and reordering (in-order delivery at a
  reduced byte rate), since every Raft message rides the Control lane's TCP; the netem bench (A3)
  covers real drops and reordering.
- F5: taken, consensus:f178d4e: append-pacing.md names the floor's bound (under ~256 KB/s a
  64 KiB frame misses the 250 ms target; under ~128 KB/s it passes rpc_timeout and is resent).

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
