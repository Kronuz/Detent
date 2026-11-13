# delta-plan-scaling: journal

State: working
Quest: .campaign/detent/quests/delta-plan-scaling.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

A1: profiling the 0.5 delta's plan at 1M. Harness: namespace-measure's T9 (docs/perf/namespace-measure-t9.* on
quest/detent/namespace-measure), copied to .scratch/h; 1M corpus derived from corpus-5m.bin by
.scratch/h/subcorpus.py (definitions with local number < 1,000,000: 1,000,004 records, sha256 b7f66891...6284).
Hypothesis from reading: TrieOverlay::Impl::remove (paged_trie_overlay.cc:168) dedupes each removed page
with a linear std::find over removed_extents, so each converted node costs O(pages touched so far);
removed_set (declared, erased in discard) is never filled. The assembler's touch() uses an index for the same key.

## Next

1. Profile job at 1M (A1); then the fix and its counting probe.

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
