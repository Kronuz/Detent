# consensus-quiesce-election-stall: journal

State: working
Quest: .campaign/detent/quests/consensus-quiesce-election-stall.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Step 1: reproduce seed 1080 and name the stalled state. Local repro: `build/consensus_driver_replace_test 1 1080 quiesce` (the test's new third argument turns Timing::quiesce on) fails as the probe did. Repro job at main: 261121-134216-fb35b7.

## Next

1. Name the cause from a per-message trace of seed 1080.
2. Fix in core.h; named test; sweep 1-3000 quiesced (Release, ASan, TSan).
3. Mutant commit restoring the stall, `--fails-without`.
4. docs/mechanisms/quiescence.md, CHANGELOG.md.

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
