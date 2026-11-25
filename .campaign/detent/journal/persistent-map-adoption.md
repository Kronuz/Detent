# persistent-map-adoption: journal

State: working
Quest: .campaign/detent/quests/persistent-map-adoption.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Library side first (persistent-map branch `erase-range`, local clone ~/code/Kronuz/persistent-map):
main's private tree grew `erase_range` (namespace drops) after persistent-map:938d65e, so the
library takes it (split/join, stored comparator), with the A8 tests and the A9 bench
(`bench/bench.cc`). Detent side: alias header, pin, CMake, `test_persistent_map_bytes.cc` (A5)
and `test_persistent_map_cost.cc` (A6 harness, built by hand against the baseline's private header).

## Next

1. Push persistent-map `erase-range`, CI green, fast-forward its main; pin that commit here.
2. Local light build of the consumer; A1/A5/A6/A9 jobs; verify A1; push.

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

<Decisions inside the quest's scope, so the reviewer can check them.>
