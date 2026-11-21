# journal-tail-cut-header-design: journal

State: working
Quest: .campaign/detent/quests/journal-tail-cut-header-design.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Prototype on journal branch `scratch/cut-header-proto` (worktree ~/.scratch/detent/journal-tail-cut-header-design/proto,
off journal:e912a28): framing.h `JOURNAL_LAYOUT` 0 as written, 1 fragment headers never straddle a
sector, 2 a turn's write ends at a sector, 3 cost emulation of per-sector checks (turn_bench only).
Probe test/cut_header_probe.cc (newest-tail Q1 shape + every block, shapes U1-U4, per-sector-check model, F3 case).

## Next

1. Build, run probe l0/l1/l2 and T9 l1/l2 as jobs; exclusive turn_bench l0-l3 jobs.
2. Write docs/design/turn-log-cut-header.md (A1) in the quest worktree.

## Blocked on

<What, and who decides. "Nothing" when nothing.>

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
