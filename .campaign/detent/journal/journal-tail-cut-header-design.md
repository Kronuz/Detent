# journal-tail-cut-header-design: journal

State: waiting on jobs 261121-132915-ad7e2d 261121-132624-b0f43f 261121-132629-eaefca 261121-132644-3269e2 261121-132652-7dd3f8 261121-132716-d4b057
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

1. Fill the doc's TABLE (probe l0 261121-132915-ad7e2d at journal:e0e9c37, l1 261121-132624-b0f43f and
   l2 261121-132629-eaefca at journal:9b4a5a5), BYTES (same jobs), LATENCY (exclusive 261121-132716-d4b057).
   Check the doc's claim that outside the newest tail unproven damage is still reported (`blocks` rows).
2. T9 under layouts 1 and 2 (261121-132644-3269e2, 261121-132652-7dd3f8): tears stay tears.
3. Recount words, run the revision pass, set "ready for review".

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

- The prototype is one commit with layouts behind `JOURNAL_LAYOUT`, built per layout, so every
  option runs the same probe source at one named commit; layout 0 is journal:e912a28's code path.
- O3's closure is a model (a damaged sector equals none of its versions), its bytes are derived
  from layout 0's measured fragments, and its write cost is an emulation (layout 3): building the
  format is the follow-up quest, not this design's.
- The O1 header-only check that would close F3 is analysed, not prototyped.
