# consensus-transport: journal

State: working
Quest: .campaign/detent/quests/consensus-transport.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Step 1, A6: rpc.h review notes N1-N4 (rpc-lane-codec-security.md) with A3's per-connection queue cap.

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

<Decisions inside the quest's scope, so the reviewer can check them.>
