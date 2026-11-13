# delta-plan-scaling review: correctness

Commit: 64546993251a
Verdict: APPROVE
Date: 2026-11-12

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

Scope: `git log main..64546993251a` without merges; the code change is
`src/detent_local/paged_trie_overlay.{cc,hh}` (Detent:2396a240) plus
`tests/detent_local/test_delta_plan_removal_probes.{cc,mutant.patch}`. Code at d660a78a (the jobs'
commit) and 64546993251a differs only in the journal.

## Evidence I relied on

- 261112-170932-ee31ea (A3, at d660a78a, base ref Detent:8aa24ceb): builds main's and the fix's
  T9 harness and compares the SHA-256 of every store file after the same block answer: IDENTICAL
  at 100K f=0.05 (30 files), 0.5 (32), 1.0 (32) and 1M f=0.5 (206). The answers carry deletes
  (256, 2553, 5040, 25335), so the removal path runs for changes and deletions. The base and fix
  differ in src/ and include/ only by the overlay change (`git diff --stat 8aa24ceb d660a78a`).
- 261112-171004-4fde22 (A4 mutant): the mutant (main's linear find, counted) prints 11.65 then
  178.3 comparisons per key for 20 then 320 pages and dies at
  `test_delta_plan_removal_probes.cc:120`, the per-key 4x bound, not at the pages assertion on
  line 119. It fails for the right reason.

## What I checked myself

Analysis, `src/detent_local/paged_trie_overlay.cc` at 64546993251a:

1. Equivalence (risk 1). `remove()` has two callers, the Convert phase (`:513`, after
   `view = inspect(current)` in `convert()`) and `copy_relocation()` (`:237`, after
   `frame.view = inspect(frame.original)` at `:247`). `inspect()` passes every old reference
   through `checked_page_bytes` (`paged_navigation.hh:20-30`), which throws unless `length`,
   `keys`, `height` and `digest` equal the page header at `(segment, offset)` in the immutable
   segment bytes. So for every reference that reaches `remove()`, `(segment, offset)` determines
   all six fields of `Reference::operator==` (`paged_layout.hh:27-33`): set membership on the
   placement key decides exactly what `std::find` with full equality decided. Two distinct extents
   cannot share a key, and one extent cannot appear under two keys. The key is the one
   `OverlayAssembler::touch` already dedupes `touched_pages()` by (`paged_overlay_assembler.cc:121`),
   so this is the existing mechanism, not a second one.
2. Consistency (risk 2). `removed_set` and `removed_extents` change together in only two places:
   `remove()` (insert into both) and `discard()` (`:727-733`, one pop and one erase per step). No
   copy (copy ctor deleted), the move moves the `StorageOwner<Impl>` pointer so `ExtentLess::probes`
   stays valid, and no reset or reuse path exists between plans. If `push_back` throws after the
   `emplace`, the set holds one extra key, but `step()`'s catch sets `failed` (`:666`), so
   `root()` and `touched_pages()` throw and the overlay can only be discarded. `impl_.reset()`
   frees the leftover node. No published byte can depend on that state.
3. Order (risk 3). No path takes a reference out of `removed_extents` except `discard`, and fresh
   nodes never carry an old reference back. A page reached again through an inline child that
   still holds its old reference, or through relocation after changes on the same overlay, is the
   only way to see a page twice, and both versions skip it. `removed_extents` therefore lists
   pages in first-removal order under both versions, whatever the input shape.

Probe job 261112-173328-6d78a4 (light, at 64546993251a, exit 0) builds the fix with an oracle in
`remove()`. On every call the oracle checks that the set's decision equals main's linear
`std::find` under full `Reference` equality and that the set and vector sizes match, and aborts if
either check fails. The job then runs every `test_local_*` test: 84 tests, all passed (one,
`test_local_trusted_paths`, skipped as usual) and no mismatch. That covers the relocation and
compaction callers (`test_paged_compaction`, `test_paged_disk_store`, the trie-overlay and
assembler tests), which A3 does not reach. (The earlier job 261112-172434-9d5b14 gave no verdict:
the script tried to build a target name that does not exist.)
Script: `.scratch/review-delta-plan-scaling-correctness/oracle.sh` with `oracle.patch`.

Counting probe (risk 4): `removal_probes()` counts comparator calls in the index the fix adds.
The mutant counts element comparisons in main's linear scan. Both count the work that grew,
deterministically, with no clock. The fix's 2.8x growth (8.4 to 23.3) sits under the 4x bound,
the mutant's 15.3x above it. The counts come from libc++. libstdc++ makes about one comparison
per tree level where libc++ makes up to two, which shifts both arms by the same factor and leaves
the ratio intact.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| C1 | no | nit | The header comment on `removal_probes()` says "spent finding pages already removed", but the counter counts every dedupe lookup, including the first insert of each new page. | `paged_trie_overlay.hh:42`, `paged_trie_overlay.cc:49-57` | Say "extent comparisons spent deduplicating removed pages". |
| C2 | no | gap | `removed_set` nodes are now charged to the overlay's storage ledger, about 56 bytes for each removed page, on top of the `Reference` in the vector. This was always intended, since the set already existed with this allocator, and the assembler's `extent_index` charges the same way. Still, a plan close to its ledger limit can now raise `StoragePressure` where main did not. It amounts to about 8 MB at 1M, matching A3's peak RSS of 820 MB on main against 828 MB with the fix, and at most about 62 MB at `Limits::pages`. No test pins a plan's admission near the limit. | `paged_trie_overlay.cc:57,95`; `storage.hh:68-80`; job 261112-170932-ee31ea peak_rss | None needed for landing. The 5M A2 round shows the real margin. |
| C3 | no | nit | The production comparator runs `++*probes` on every set comparison so a test can read it. This costs one increment through a pointer, which is negligible beside the comparison itself, and follows the existing `visited_pages()` metric. | `paged_trie_overlay.cc:49-55` | Keep. |

## Not run

- Linux, libstdc++ and the sanitizers: those are CI's (CI at 64546993251a is running).
- A2 timings and the 5M exclusive round: the contributor's jobs own them.
- The full suite: the lead's suite job.
