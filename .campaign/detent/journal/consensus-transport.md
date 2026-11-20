# consensus-transport: journal

State: waiting on jobs 261120-104122-60d46f 261120-102517-d9c5e8 and the strict A/B jobs (095040-68a827..095143-778a7a, 100645-55c3f1..100652-5c2a3c); then A5
Quest: .campaign/detent/quests/consensus-transport.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Head consensus:06b9a76 (pushed). Steps 1-4 and A4 coded. Next: A5 (port fetch-alongside-consensus
onto lane fetch, SIGSTOP inside a drain with a never-drain mutant, `timing` label, three macOS
x86_64 CI runs), then transport-admission, then groups-upload-stall-cause; when the strict jobs
finish, write group-bytes' and receive-bound's before/after rows into docs/perf/transport-idle-cost.md;
re-run every evidence job at the final head before review.

Results in: suite 261120-102515-dc5d23 at 975c51d exit 0 (every consensus_transport_ entry);
fails-without done: hold-ignored -102022-16b298 (exit 1), rpc-no-drain -102028-2bc70b (exit 1),
no-credit-on-refuse -100643-062f07, data-unbounded -100637-57c2d3, control-unbounded
-100640-082b18 (134); coverage -103119-56d7ac: rule 7.6 1, pool waits reached (ticket 6, granted
5). lane-via-other-loop -102035-9cd55a exited 139 (a dangling Link in the mutant, not the latency
the case asserts): the mutant now hands frames to the peer's current link
(consensus:50f0a48), rerun 261120-104122-60d46f.

A4 at consensus:975c51d: `fetch(node, name, done)` is method 1; `RpcOptions::reader` serves it
(D8 table). Local: rpc_host fetch: 16 MiB exact in 1.1 s beside 131 MiB of group uploads on the
same Bulk connection, heartbeats' longest gap 210 ms, every D8 row. Jobs: suite
261120-102515-dc5d23 (--for A1,A2,A4, at 975c51d), TSan -102517-d9c5e8.

A6 N4 at consensus:06b9a76: fuzz job 250bdc reached the pool waits (ticket 427, granted 299,
roomless 10) but not rule 7.6 (0); seed-past-credit reaches it (replay: "rule 7.6: past credit").
Coverage job 261120-103119-56d7ac.

Step 4, A1/A2, at consensus:895b3c6: `hold(node)`/`release(node)` (T3), lane_idle on a Bulk
connection that carried no group frame (`RpcOptions::idle`, 60 s, D7), Drain then graceful close
from the feed once the table drained (T8; `drain_deadline` 5 s aborts), calls on a drained lane
wait for the next connection, `drain()`/`lanes_drained()` for shutdown. Local: rpc_host hold
(closed 302 ms after release), idle-race (5 of 5 not_processed, 0 aborted, next call ok), shutdown
(4 MiB echo ok, later call not_processed), reader (T5, slowest 0 ms) pass. Jobs: suite
261120-102012-185754 (--for A1,A2; supersedes -100634-06fa3e); TSan -102043-e1cc69 (supersedes
-100655-f3cd32); fails-without hold-ignored -102022-16b298, rpc-no-drain -102028-2bc70b,
lane-via-other-loop -102035-9cd55a. T7 evidence: rpc_test late-deadline (in the suite).

Step 3, transport-receive-bound, at consensus:1d24b8a: Lane::arriving per group; data past the
window ends the connection (overrun), control past `arriving` (256) refused (flooded). Local:
receive-bound passes (4 of 8 data delivered, 1 overrun; 256 of 1,000 control, 744 flooded), all
45 consensus_transport_ ctest entries pass. Jobs: suite 261120-100634-06fa3e (head; covers steps
2-3); TSan -100655-f3cd32; fails-without data-unbounded -100637-57c2d3, control-unbounded
-100640-082b18, no-credit-on-refuse (moved) -100643-062f07; hop strict main e1f249d control
-100645-55c3f1 / data -100647-f05401, head control -100649-2aa87f / data -100652-5c2a3c.

Step 2, transport-group-bytes, at consensus:0a65af1. Local: inflight 10K 594.7 B per group in
flight (4,746.1 at c2b0133); idle group stored 36.0 B, its frames 52.4. Done: fails-without
owed-before-control 261120-095127-6b5847, owing-keeps-prefix -095130-ab25d2, closes-unbounded
-095133-f42ea5 (all exit 134). Queued strict before (c2b0133) / after (0a65af1): inflight
-095136-7a0672 / -095040-68a827, hop control -095138-5864ca / -095043-dc00e8, hop data
-095141-7fd45f / -095046-6fe0ef, idle bytes -095143-778a7a / -095049-c97307. Feed-scan N1, N4:
dispositions written in docs/perf/transport-idle-cost.md (A2: hop CPU).

Step 1 (A3 + A6) at consensus:3284055 + 84e199c: done 0a0f0c (lane suite), ede104 (ASan); fails-
without 5203e3, f32f32, b70222, 42250f, 8e73c6, 3aaf61 all exit 134/8; 250bdc (fuzz+coverage)
running. Re-run the evidence at the final head before review.

Pre-existing (main): mutants no-extension, uncapped, twice in test/transport/mutant_commit.py no
longer apply to deadline.h (changed by consensus:e7d615c on main); for the lead.

Interface change (consensus-quiescence merges this branch): `HostTransportOptions::open_of`
(`std::function<GroupOpen(GroupId)>`) is required beside `member_of`; `store(group, incarnation)`
drops its open argument; new options `closes` (4096), `arriving` (256) and stats `owed`,
`unanswered`, `overrun`, `flooded`. Frames on a link its lane replaced are refused in arrived().
Step 4 adds `hold`, `release`, `linked`, `drain`, `lanes_drained`, `RpcOptions::idle` and
`drain_deadline`, `LaneStats::drains`/`retired`, and `rpc::Lane::draining()`. A4 adds
`fetch(node, name, FetchDone)`, `RpcOptions::reader`/`fetches`/`fetch_bytes`/`fetch_deadline`,
`fetch_method` (1) and `fetch_missing` (Error 256); `handle(1)` throws when a reader is set.

## Next

Code: /Users/kronuz/code/Kronuz/consensus-wt/detent/consensus-transport (consensus, branch
quest/detent/consensus-transport). Local build: `.scratch/build` there (Ninja, Release, transport on).

1. A6 + A3 in rpc.h: N3 deferred close inside callbacks; N2 `send` past a known total; N1+A3 every
   queued frame counts toward a per-connection cap (`LaneOptions::queue_bytes`), chunks wait for
   room before charging `rpc_outgoing_bytes`; N4 fuzz harness pools below two windows, max_len.
2. transport-group-bytes: control before owed credit (N2), repaid prefix freed (N3), vector FIFO
   for Active queues, one GroupTable lookup per control frame, bounded closes, idle group <= 64 B
   via `open_of(group)`; bench before/after.
3. transport-receive-bound: bound `arrived()` per group per loop (window of data, fixed control).
4. A1/A2: hold/release, lane-only `lane_idle` with Drain, graceful close (flushed frames, Drain,
   close_notify), T6.
5. A4/A5: fetch as lane method 1; fetch-alongside-consensus ported; SIGSTOP inside a drain.
6. transport-admission: N9 configured-peer priority, N10-N13, N5-N7, N14.
7. groups-upload-stall-cause: needs a captured CI failure (lead watches).

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| <A1> | <from `campaign brief`> |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

- R1 (A6 N1, A3) Every frame a call table queues is charged to `rpc_outgoing_bytes`, header
  included. Call and Body frames wait for room (`LaneOptions::queue_bytes`, 4 frames) and then
  for the pool; small frames (Credit, Cancel, Error, Drain) answer the peer and cannot wait, so
  they take the charge at once past the limit (`ByteBudget::charge`; `take` gives nothing while
  `used >= limit`, `renew` keeps its own bytes first). Their bound: one per live call except
  refusals, and more than `serving` refusals unsent is a ProtocolError (the reviewer's option a).
- R2 (A3) The per-connection cap is the table's queue, not the connection's: the connection already
  takes one 64 KiB feed only once its own queue is out, so a stalled peer holds at most
  `queue_bytes` + the frame in hand of the host pool. The host refuses `outgoing_bytes` below one
  frame (65,584), since every frame is now charged whole.
- R3 (A6 N3) Deferred close (option 2): a `Busy` depth guard on every entry point; `close()` inside a
  callback runs when the outermost returns. `finish` also returns on an unknown handle.
- R5 (group-bytes A2) D108's 64 B is the stored part, as D112 splits it (fixed + per-peer): an
  idle group is stored as id, incarnation, serial (24 B minimum, 36.0 measured); per peer it keeps
  32 B minimum (16 B on the group loop with the last reported source, 16 B on the connection's).
  The idle bench now fails stored > 64 and a group over `44 + 64P` (was `164 + 52P`).
- R6 (group-bytes A1-A2) The folded tasks' criteria have no ids in the tool; their jobs carry
  `--for A1` (the feed criterion) and name the task criterion in `--what`; the Evidence rows below
  map them. "One lookup per control frame": the feed's pop hands the frame its Active (was a
  find in pop, then one in settle), and a receive finds the opened group once.
- R7 (identity N1) Past `closes` owed closed answers a refusal goes unanswered (`unanswered`); the
  record's credit is kept, and the peer's next frame is refused and answered again.
- R8 (A1, D7) lane_idle closes only a Bulk connection that has carried no group frame either way
  (`Lane::grouped`): one that carried a group stays until `stop()`, as Control does, so D108's
  steady state (both lanes per peer) is unchanged.
- R9 (A2, T8) Shutdown is `drain()` then `stop()`; `stop()` still aborts. A call made while the
  loop drains ends `not_processed` (safe to retry elsewhere).
- R10 (A2, T5) T5's mutant hands lane frames through another loop (`lane-via-other-loop`); the
  `reader` case holds that loop and fails it.
- R11 (A4, D8) The fetcher enforces its `fetch_bytes` when the bytes it holds would pass it (a
  Cancel message_size), not at the first Body's total, which the lane's events do not expose. A
  fetch a drain refused (`not_processed`) ends `resource_unavailable_try_again`, safe to retry.
- R4 `id-at-call`'s case separates ids by room instead of by a blocked pool: with every frame
  charged, an empty request no longer skips the pool's FIFO.
