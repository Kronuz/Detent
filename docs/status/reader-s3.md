# Reader S3: verified-page bitmap in LazyPageAccess

> Status: DRAFT (2026-11-03) Branch `phase/reader-s3`, worktree `Detent-wt/reader-s3`; reader design D3 inside `LazyPageAccess` (step S3 of `docs/design/reader.md`). How it works: `docs/mechanisms/verified-page-bitmap.md`.

**Done criteria (lead's brief):** the per-generation `std::mutex`, hash map and LRU in `lazy_page_access.cc` are replaced by D3's lock-free segment table and per-segment verified bitmap; a 16-thread TSan test hammers first touch and repeated lookups; the bitmap orderings are justified in a comment; a relaxed-ordering mutant and a skipped-verification mutant each fail a test; a corrupted page is refused on first touch; ASan+UBSan in a separate build; existing tests pass; a smoke A/B against the parent on an 8-thread scattered lookup under `quiet strict`; CI green; not merged.

Inputs folded in: the D3 re-check on main (`Detent:b55023b`, "Re-check" in `docs/reviews/reader-design-preliminary.md`): the dropped digest compare is ruled sound; the granule argument rests on the validators' cell marks and `offset % 8 == 0`, `length >= 80`; a miss at the cap keeps today's behavior (throw, generation failed) with a test pinning it; segment identity means immutable bytes. The design's new cap behavior on main (`Detent:b607b5c`, `DETENT_NO_MEMORY` for one lookup) is S6's, not S3's.

## State

Implementation, tests, ledger and mechanism note committed; sanitizer builds, mutants, full ctest, A/B and CI in progress.

## Done

1. `Detent:19cda59`: D3 in `LazyPageAccess` (segment table, verified bitmap, `page_verifications()`, `LazyPageTesting` hook); `lru_cache` no longer linked; `test_local_lazy_page_bitmap`; the lazy page test's forged-digest assertion follows the ruling (a forged digest at a verified offset is served; a forged reference at an unverified offset is refused and fails the generation).
2. `Detent:043058c`: a miss re-probes before refusing at the pack count or the cap. Found by the first stress runs: a thread whose segment another thread published after its probe saw the count at `root.segments` and failed the generation (2 spurious refusals in one 16-thread run). Regression test `raced_admission` (count and cap variants) with a `Miss` window.

## Next

1. TSan and ASan+UBSan runs; mutants; full Debug ctest.
2. Smoke A/B, push, CI.

## Design vs code

1. Segment objects belong to one generation's `LazyPageAccess`. D3's sharing of a kept segment object across a namespace's generations, freed by the D5 freer, belongs to the S6 core; here `retire_step` frees them.
2. Bitmaps and the segment table are charged to the heap ledger, as the LRU was; D3 counts bitmaps against the mapped-bytes cap (Q5, T4), which is S6's.
3. Refusing past the pack count or the cap: a pre-check before mapping (with a re-probe for raced segments) and a recount by the publish winner. Two new segments admitted at once can both be mapped before the winner's recount refuses one and fails the generation; today's serial path never mapped the excess segment. The outcome (refused, generation failed) is unchanged.
4. Heap pressure on a miss: today evicted one verified-page entry and retried; there is nothing to evict now, so the `StoragePressure` becomes "admission exhausted" (generation not failed) at once.
5. `resolve` refuses a reference shorter than a page header before the bit test, which keeps the granule inside the bitmap; before, `verify_page` refused it.
6. The table has `bit_ceil(2 × root.segments)` slots (at least 2), not exactly `2 × root.segments`, so probing masks instead of dividing.
7. `LazyPageTesting` (a qualification hook at the miss, publish and mark windows) is new; it costs one relaxed load on a miss.

## Blocked on

Owner decisions, not blocking the branch:

1. `ViewLimits::verification_cache_bytes` (`include/detent_local/view.hh:36`) is now read by nothing. Removing a public field narrows the API. Options: (a) remove it in S3; (b) keep it until legacy step H deletes the view API; (c) mark it deprecated now. Recommendation: (b), with D3's note that the bitmaps replace it.
2. `cmake/dependencies.json:231` still declares `lru_cache`; nothing fetches or links it now. Removing the entry is a pin-tooling change; recommendation: drop it with (1).
