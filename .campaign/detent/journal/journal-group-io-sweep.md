# journal-group-io-sweep: journal

State: working
Quest: .campaign/detent/quests/journal-group-io-sweep.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Fix written in journal-wt (host.h: one range per group in a std::map, batched sweep at
2x live; test/host_test.cc `view_sweep`). Building and running host_test locally before
committing; then mutants and jobs.

## Next

1. Commit fix + test; submit suite (A1), two mutant --fails-without jobs (A2), exclusive bench
   A/B with --ref base=ea397ab (A3), then the doc table (A4).

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
