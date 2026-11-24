# consensus-drop-memory-io-include: journal

State: ready for review
Quest: .campaign/detent/quests/consensus-drop-memory-io-include.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Nothing: consensus:c177c88 runs the test on faultfs (5b1ffb6 dropped an include the test used: CI red); CI remains.

## Next

1. Land on green CI (the lead).

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
| A1 | job 261123-170615-153c05 (no memory_io or MemoryIO; 10 of 10 install_request cases pass at consensus:c177c88); `ci:ci` |

Failed: 261123-164556-dd9522 passed at 5b1ffb6 but CI failed there (jt::Disk, jt::MemoryIO still used).
