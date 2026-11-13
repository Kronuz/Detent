> Status: DRAFT (2026-11-12). Cause found and a candidate fix screened; the confirming jobs are pending.

# A delta's plan, linear in the keys it changes

A block answer's plan phase grew superlinearly with the store: a 0.5 C delta planned in 0.75 s at 100K
(50,035 keys, 15 µs per key) and 512.8 s at 5M (2,499,245 keys, 205 µs per key; job 261112-144057-f1c829),
while the 5M full copy planned in 10.6 s.

## Cause

`TrieOverlay::Impl::remove` (`src/detent_local/paged_trie_overlay.cc`) runs once for every base node the
overlay converts to a fresh one. The nodes of one page share its reference, so it skips a page already
removed, and it found that page with a linear `std::find` over `removed_extents`, every page removed so far.
A delta changing a fraction f of a store of P pages converts about two nodes per changed key and touches
most pages once f passes a few percent, so each key costs O(P) reference comparisons and the plan
O(keys x P). The `removed_set` index declared beside the vector, and emptied by `discard`, was never filled.

Profile (macOS `sample`, 15 s of the plan, 0.5 delta at 1M on main's code, job 261112-170345-ea0ce8, plan
44.9 s under a light hold): `remove` is the top frame in 7,828 of the 10,346 working samples (76%); next is
`verify_page_structure` in 818 (8%, first-touch verification, once per page); `TrieOverlay::step`,
`TrieOverlay::keys` (the in-page key count of a converted inline node, bounded by the page) and page
decoding follow, none above 4%.

## Fix

`remove` finds a removed page in `removed_set`, keyed by the page's placement (segment, offset), the key
the overlay assembler's `extent_index` already uses for the same pages. `removed_extents` keeps its order,
so `touched_pages()` and the published bytes are unchanged. A counting comparator exposes the index's
comparisons as `TrieOverlay::removal_probes()`.

## Counting probe

`tests/detent_local/test_delta_plan_removal_probes.cc` changes every definition of a 2,000- and a 32,000-key
store (20 and 320 pages removed) and counts the extent comparisons per changed key:

| Build | 2,000 keys | 32,000 keys | Growth for 16x the pages |
| --- | --- | --- | --- |
| Main's linear search (the mutant) | 11.7 | 178.3 | 15.3x |
| The index | 8.4 | 23.3 | 2.8x |

The test requires growth of at most 4x.

## Screen (local, light hold, one run each; not evidence)

Main's harness against the fix's, the same answer (0.5 C, seed 1009) on clones of one prepared store:

| Corpus | Keys changed | Plan, main | Plan, fix | Per key, fix |
| --- | --- | --- | --- | --- |
| 100K | 50,035 | 0.749 s | 0.656 s | 13.1 µs |
| 1M | 499,922 | 24.05 s | 6.92 s | 13.8 µs |

The 100K and 1M stores published by main's and the fix's harness are byte-identical, every file of them.

## Method

Harness: namespace-measure's T9 (`docs/perf/namespace-measure-t9.*` on `quest/detent/namespace-measure`).
Corpora: `corpus-100k.bin` (SHA-256 3214561e...a5740), `corpus-5m.bin` (bb87ae5d...c835), and a 1M corpus
derived from the 5M one: its definitions whose local number is below 1,000,000 with every namespace record,
1,000,004 records (b7f66891...6284). Host: Intel i9-9980HK, macOS 15, x86-64 Release build.
