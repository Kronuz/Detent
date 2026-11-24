# consensus-fetch-oversize-abort: journal

State: working
Quest: .campaign/detent/quests/consensus-fetch-oversize-abort.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Hunting the cause. Facts so far:
- This host is x86_64 (i9-9980HK), not arm64: same arch as CI's macos-x86_64.
- Not reproduced alone: 40/40 plain (run1), 48/48 at 6 at once beside 12 burners (prun-084913,
  table phase 1.5-2.2 s, no connection failure before teardown).
- CI: every failing macos-x86_64 run ran rpc_host_fetch beside rpc_host_stalled-peer (ctest
  --parallel 2): 45407143806 a2 (abort, 8.06 s; fetch 16:08:47.6-55.6, stalled-peer
  49.5-54.0), a1 (heartbeat gap 4976 ms), 45366654906 main (gap 1564 ms, message_size fine).
  stalled-peer holds host 2's loop 4 s while host 1 fills its socket.
- A passing macos-x86_64 fetch takes 1.54 s (45415901609); the abort run took 8.06 s.
- Hypothesis: loopback socket memory pinned by the concurrent stalled-peer starves the fetch's
  Bulk connection (ENOBUFS on send, fatal in socket_writes, or a 5 s stuck-rule expiry), and
  the connection's end aborts every call on it. Probe: PROBE prints in connection.h (errno,
  stuck-rule values), beside.sh runs fetch beside stalled-peer (run3).
Note for the lead: run 45366654906 on main failed on the heartbeat gap, and its 48 MiB fetch
ended "Message too long" as expected; the abort is seen only on 45407143806 attempt 2.

## Next

1. Reproduce with diagnostics (malformed, ConnectionEnd, Lane ProtocolError text).

## Blocked on

Nothing

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), name every job you examined, failures included (`campaign brief` lists failed
jobs no journal names), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| <A1> | <from `campaign brief`> |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

<Decisions inside the quest's scope, so the reviewer can check them.>
