# consensus-fetch-oversize-abort: journal

State: working
Quest: .campaign/detent/quests/consensus-fetch-oversize-abort.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Reproducing locally: build/ in the consensus worktree (RelWithDebInfo, ninja), target
consensus_transport_rpc_host_test, case `fetch`, in a stress loop under quiet light.
Reading so far: the client (one) stops `huge` itself at 32 MiB (HostTransport::fetch body ->
stop(message_size) -> Lane Cancel, outcome cancel, mapped to message_size). connection_aborted
only comes from Lane::close() via close_rpc: the Bulk link ended (rpc ProtocolError -> malformed
+ abort, group overrun -> abort, or the Connection failing). CI's line shows overrun 0 and
links 2+2 (no redial), malformed is not printed.
Note for the lead: run 45366654906 on main failed on the heartbeat gap (1564 ms of 1013), and
its 48 MiB fetch ended "Message too long" as expected; the abort is seen on run 45407143806.

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
