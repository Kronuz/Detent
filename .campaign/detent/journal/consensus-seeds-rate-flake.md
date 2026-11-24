# consensus-seeds-rate-flake: journal

State: waiting on jobs 261124-041541-5d4555 261124-041545-a9b999 261124-041548-778dca 261124-041551-dab805
Quest: .campaign/detent/quests/consensus-seeds-rate-flake.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Fix committed on the quest branch (consensus:a2f6eb3): rate() samples the host's lag (a local `Lag`
copy, rpc_hosts.h's shape with stop()) and bounds each gap by 150 ms + lag; lower bound stays 90 ms.
Cause: the seed timer is re-armed after each send, so the unstalled gap is interval..interval+jitter
(100..150 ms); the raw listener timestamps in the same single-threaded loop, so a process stall
(CI runner pause) lands in one gap as stall time on top: 150 ms + 100 ms slack fails past 250 ms.
It measures the runner, not the rate limit. Scripts: ~/.scratch/detent/consensus-seeds-rate-flake/.

## Next

1. Read the four jobs (A1 main stall fails, A2 head stall passes, A2 no-interval mutant fails, A3 50 runs); confirm none is a skip (exit 77) in disguise.
2. Check each fails-without log (A1 main, A2 mutant) shows the assertion line, not exit 77; say so in the Evidence row.
3. Lead (msg): when consensus-rpc-host-stream-gap lands test/transport/lag.h, merge consensus main, include lag.h in seeds_test.cc, delete the local Lag copy (jobs stand until then).
4. Paste Evidence, Result; State "ready for review" (waits for the lag.h swap and CI before landing).

## Blocked on

<What, and who decides. "Nothing" when nothing.>

Nothing.

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

- rpc_hosts.h cannot be included (its global `fixture`/`context` collide with seeds_test's own, and
  it is outside my file set), so seeds_test.cc carries a 20-line copy of `Lag`. Follow-up for the
  lead: one shared lag header once consensus-rpc-host-stream-gap lands.
- The burst bound (`heard[2] - start < 80ms`) and the datagram floor take the same lag, one shape.
