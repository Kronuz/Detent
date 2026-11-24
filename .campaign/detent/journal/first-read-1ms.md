# first-read-1ms: journal

State: working
Quest: .campaign/detent/quests/first-read-1ms.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

A1: profiling the core's fresh first read. Local builds of both trees under `.scratch/fr/out`
(gate `prepare`, main from a detached scratch worktree `.scratch/main-tree`); a scratch-only probe
header `.scratch/fr/probe/frp.hh` marks each sub-step (never committed).

## Next

1. A1 profile job of the slowest cell, top costs named with numbers.
2. Remove the top costs one at a time, each screened with an interleaved A/B against main.
3. reader_bench hot path (1 and 8 threads, hot and scattered) against main.
4. A2 exclusive gate runs (stopped, running) at the final head; A3 via `campaign verify` and CI.

## Blocked on

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

- R1: the gate runs the reader core in both trees once a tree has `src/detent_reader` (reader-s6
  landed): this quest's A/B is core against core; main before reader-s6 still runs its Client.
