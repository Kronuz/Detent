# consensus-rpc-host-stream-gap: journal

State: waiting on jobs 261124-013928-c5f6e7
Quest: .campaign/detent/quests/consensus-rpc-host-stream-gap.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

A1 probe: commit consensus:a61d466 makes the stream case print, for each heartbeat gap over 300 ms, the stage (post->send on loop 1, send->deliver) and the test thread's post gap. Job 261124-013928-c5f6e7 runs it beside 16 burners, 4 copies at once (script ~/.scratch/detent/consensus-rpc-host-stream-gap/repro.sh).

## Next

1. Read the stage split: test thread late (post gap), loop 1 late (post->send), or the wire/loop 2 (send->deliver). Fix that cause; the Lag-based bound the fetch case uses (1000 + lag.worst) is the repo's own pattern for host stalls.

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
