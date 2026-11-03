# Reader S3: verified-page bitmap in LazyPageAccess

> Status: DRAFT (2026-11-03) Branch `phase/reader-s3`, worktree `Detent-wt/reader-s3`; implementing reader design D3 inside `LazyPageAccess` (step S3 of `docs/design/reader.md`).

**Done criteria (lead's brief):** the per-generation `std::mutex`, hash map and LRU in `lazy_page_access.cc` are replaced by D3's lock-free segment table and per-segment verified bitmap; a 16-thread TSan test hammers first touch and repeated lookups; the bitmap orderings are justified in a comment; a relaxed-ordering mutant and a skipped-verification mutant each fail a test; a corrupted page is refused on first touch; ASan+UBSan in a separate build; existing tests pass; a smoke A/B against the parent on an 8-thread scattered lookup under `quiet strict`; CI green; not merged.

## State

Implementation and tests written; local builds in progress.

## Done

## Next

1. Debug build and the two lazy-page tests; TSan and ASan+UBSan builds; mutants; full ctest.
2. Smoke A/B, push, CI.

## Design vs code

## Blocked on

Nothing.
