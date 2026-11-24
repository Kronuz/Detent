# consensus-rpc-host-stream-gap: journal

State: waiting on jobs 261124-014655-15443a 261124-014707-1319b1 261124-014657-c7b211
Quest: .campaign/detent/quests/consensus-rpc-host-stream-gap.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Cause found (host stall, not lane scheduling): the stream test stopped 1.5 s mid-run (SIGSTOP, job
261124-014521-c32cc2) shows gap 1530/1574/1527/1574 ms = the host's lag (1510-1517 ms), the
heartbeat's own posting thread frozen (post gap = delivery gap), post->send 0 ms and send->deliver
0-1 ms: nothing waited behind Bulk. Unstalled, gaps stay 103-222 ms (16-24 burners, niced, 4 at a
time: jobs 261124-013928-c5f6e7, 261124-014534-123e12). CI's failure fits: 1,170 ms of a 1,593 ms
stream, the rest at 76 MB/s against 55 MB/s unloaded. Fix: consensus:303455c/93df08c give the stream
case the fetch and group-full cases' allowance, 1,000 ms plus the Lag sampler's worst.
Jobs for A2 (stopped run at fix 261124-014655-15443a, at main 261124-014707-1319b1 fails-without) and
A3 (261124-014657-c7b211).

## Next

1. Paste `campaign brief`'s table; Result; push; State ready for review.

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
