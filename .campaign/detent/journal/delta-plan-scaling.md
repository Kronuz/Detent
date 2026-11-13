# delta-plan-scaling: journal

State: blocked on lead: the fix is in src/detent_local/paged_trie_overlay.{cc,hh}, outside the file set
Quest: .campaign/detent/quests/delta-plan-scaling.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

A1 done (cause named, profile job 261112-170345-ea0ce8). The fix is written and screened but uncommitted: it
edits src/detent_local/paged_trie_overlay.{cc,hh}, outside the file set. The whole candidate (overlay fix,
test, CMake registration, mutant patch) is in the worktree's working tree and saved as
.scratch/candidate.patch (apply with `git apply` after `git -C <wt> checkout .` if the tree is lost).

Cause: TrieOverlay::Impl::remove (paged_trie_overlay.cc:168) runs per converted base node and dedupes the
page with a linear std::find over removed_extents (all pages removed so far): O(keys x pages). removed_set
(declared, emptied in discard) was never filled. Fix: emplace (segment, offset) into removed_set, the
assembler's extent_index key; removed_extents keeps its order, so touched_pages() and the bytes are unchanged.
A counting comparator gives TrieOverlay::removal_probes().

Screen (local, light, one run, main's harness vs the fix's on clones; not evidence): 100K 0.5: plan 0.749 ->
0.656 s (13.1 us/key); 1M 0.5: 24.05 -> 6.92 s (13.8 us/key). Probe test: 8.4 -> 23.3 comparisons/key for
16x the pages (2.8x), the mutant (main's linear search, counted) 11.7 -> 178.3 (15.3x); test bound 4x.
Byte identity: 100K 0.5, every file of main's and the fix's published store identical (local).

Prepared for after the ruling, in .scratch/h: namespace-measure-t9.{sh,cc,cmake} (copied from
namespace-measure's branch), profile.sh (A1), identity.sh (A3: builds main via --ref base and the fix,
compares every store file per corpus and fraction), subcorpus.py (the 1M corpus, .scratch/corpus-1m.bin,
sha256 b7f66891adadebd7da65c09f36b93251bd8f37d9343423a447adbe276b1f6284).

## Next

1. On the ruling: commit the candidate (fix + test + CMake + tests/detent_local/test_delta_plan_removal_probes.mutant.patch).
2. Jobs: `campaign verify delta-plan-scaling --for A4`; mutant `--for A4 --fails-without -- tests/detent/one_knob_mutant.sh
   tests/detent_local/test_delta_plan_removal_probes.mutant.patch test_local_delta_plan_removal_probes`; A3 identity.sh
   with `--ref base=main` (100K f 0.05,0.5,1; 1M f 0.5); A2 light prep (harness build + 100K/1M/5M prepared stores)
   then strict `round 1 full 0.5` at 100K and 1M, then one exclusive 5M `round 1 full 0.5` (T9_BUDGET_S ~300).
3. Fill docs/perf/delta-plan-scaling.md's after table from the jobs; delete .scratch stores and builds.

## Blocked on

Lead: widen the file set to src/detent_local/paged_trie_overlay.cc and .hh (the cause and the 6-line fix live
there; nothing in the current set can change it: paged_delta_plan.cc only hands keys to the overlay). Options:
(1) widen the set (recommended; the overlay's sibling, paged_overlay_assembler, already indexes the same pages
this way); (2) move the quest to whoever holds the overlay. No contract or byte change either way.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | 261112-170345-ea0ce8 (profile at 1M, main's code): TrieOverlay::Impl::remove, a linear std::find over every page removed so far per converted node, 7,828 of 10,346 working samples (76%); O(keys x pages). Details: docs/perf/delta-plan-scaling.md |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

## Rulings I made

- 1M corpus: derived from corpus-5m.bin (definitions with local number < 1,000,000, every namespace record),
  .scratch/h/subcorpus.py; no 1M corpus existed.
- The profile is a light job: sampling shares hold under load; the timing criteria (A2) run strict/exclusive.
- Probe test asserts growth (per-key comparisons at 32K keys <= 4x those at 2K), not an absolute count, so
  it holds across std::set balancing details.
