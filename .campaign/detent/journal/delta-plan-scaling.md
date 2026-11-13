# delta-plan-scaling: journal

State: ready for review
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

Review.

Committed: fix 2396a240, merged main d660a78a. File set widened by the lead (overlay).

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | 261112-170345-ea0ce8 (profile at 1M, main's code): TrieOverlay::Impl::remove, a linear std::find over every page removed so far per converted node, 7,828 of 10,346 working samples (76%); O(keys x pages). Details: docs/perf/delta-plan-scaling.md |
| A5 | `docs/perf/delta-plan-scaling.md`: "Before and after: plan per size (100K 0.782 -> 0.675 s, 1M 23.89 -> 6.919 s, 13.5 and 13.8 us/key after), byte identity, ledger margin, 5M 512.8 -> 34.855 s (13.9 us/key)" |

## Result

Cause: `TrieOverlay::Impl::remove` deduped removed pages by a linear `std::find` over `removed_extents`,
O(keys x pages). Fix (detent:2396a240, comment detent:cd4ecda1): look the page up in `removed_set`, keyed by
(segment, offset); order and bytes unchanged. Plan phase of the 0.5 delta (docs/perf/delta-plan-scaling.md):

| Corpus | Plan before | Plan after | Per key after |
|---|---|---|---|
| 100K | 0.782 s | 0.675 s | 13.5 us |
| 1M | 23.89 s | 6.919 s | 13.8 us |
| 5M | 512.8 s | 34.855 s | 13.9 us |

Jobs: A2 261112-170955-436de9, 261112-170958-40ef49, 261112-175733-62e242 (5M); A3 261112-170932-ee31ea
(byte-identical at 100K and 1M); A4 pins 261112-170935-8c7bb4 and mutant 261112-171004-4fde22. Leaves open: a 0.5
delta at 5M still costs 2.5x a full copy's apply (36.986 vs 14.864 s), T9's question. Ledger charge about 56 bytes
per removed page (about 40 MB at 5M).

## Rulings I made

- 1M corpus: derived from corpus-5m.bin (definitions with local number < 1,000,000, every namespace record),
  .scratch/h/subcorpus.py; no 1M corpus existed.
- The profile is a light job: sampling shares hold under load; the timing criteria (A2) run strict/exclusive.
- Probe test asserts growth (per-key comparisons at 32K keys <= 4x those at 2K), not an absolute count, so
  it holds across std::set balancing details.
