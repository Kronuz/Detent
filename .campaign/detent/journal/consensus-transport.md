# consensus-transport: journal

State: working
Quest: .campaign/detent/quests/consensus-transport.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Step 2, transport-group-bytes. Step 1 (A3 + A6) is at consensus:3284055 (+ consensus:84e199c, the
coverage script); its jobs are queued: suite of the lane 261120-092624-0a0f0c, ASan
-092627-ede104, fuzz+coverage -092745-250bdc; fails-without rpc-no-queue-cap -092630-5203e3
(queue-cap) and -092632-f32f32 (stalled-peer), refusals-unbounded -092635-b70222,
send-past-total -092638-42250f, close-reenters under ASan -092641-3aaf61, id-at-call (case
rewritten by room) -092649-8e73c6.

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
- R4 `id-at-call`'s case separates ids by room instead of by a blocked pool: with every frame
  charged, an empty request no longer skips the pool's FIFO.
