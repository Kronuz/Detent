# Reader S3: verified-page bitmap in LazyPageAccess

> Status: DRAFT (2026-11-03) Branch `phase/reader-s3`, worktree `Detent-wt/reader-s3`; reader design D3 inside `LazyPageAccess` (step S3 of `docs/design/reader.md`). How it works: `docs/mechanisms/verified-page-bitmap.md`.

**Done criteria (lead's brief):** the per-generation `std::mutex`, hash map and LRU in `lazy_page_access.cc` are replaced by D3's lock-free segment table and per-segment verified bitmap; a 16-thread TSan test hammers first touch and repeated lookups; the bitmap orderings are justified in a comment; a relaxed-ordering mutant and a skipped-verification mutant each fail a test; a corrupted page is refused on first touch; ASan+UBSan in a separate build; existing tests pass; a smoke A/B against the parent on an 8-thread scattered lookup under `quiet strict`; CI green; not merged.

Inputs folded in: the D3 re-check on main (`Detent:b55023b`, "Re-check" in `docs/reviews/reader-design-preliminary.md`): the dropped digest compare is ruled sound; the granule argument rests on the validators' cell marks and `offset % 8 == 0`, `length >= 80`; a miss at the cap keeps today's behavior (throw, generation failed) with a test pinning it; segment identity means immutable bytes. The design's new cap behavior on main (`Detent:b607b5c`, `DETENT_NO_MEMORY` for one lookup) is S6's, not S3's.

## State

Ready for code review. Code head `Detent:c5056c1`: CI green on every job (run 41752136575: linux-x86_64-gcc, linux-arm64-gcc, linux-x86_64-clang, linux-asan-ubsan, linux-tsan, macos-arm64, macos-x86_64, linux-benchmarks; headers and freebsd skipped by the workflow). Later commits are docs-only.

## Done

1. `Detent:19cda59`: D3 in `LazyPageAccess` (segment table, verified bitmap, `page_verifications()`, `LazyPageTesting` hook); `lru_cache` no longer linked; `test_local_lazy_page_bitmap`; the lazy page test's forged-digest assertion follows the ruling (a forged digest at a verified offset is served; a forged reference at an unverified offset is refused and fails the generation).
2. `Detent:043058c`: a miss re-probes before refusing at the pack count or the cap. Found by the first stress runs: a thread whose segment another thread published after its probe saw the count at `root.segments` and failed the generation (2 spurious refusals in one 16-thread run). Regression test `raced_admission` (count and cap variants) with a `Miss` window.

3. `Detent:c5056c1`: the bitmap test takes a case name, so each mutant is shown to die in its own case.

## Proof (macOS x86_64, Apple clang, local builds under `quiet light`)

1. **16-thread stress** (`stress` in `tests/detent_local/test_lazy_page_bitmap.cc`): 16 threads, 26 namespaces (27 segments), each resolve checked byte for byte against the builder's bytes (the sequential model), one mapping address per segment across threads, trie walks for every namespace; then 64 warm rounds per thread. The verification count after first touch lies between the serial run's count (55 pages) and 16 times it, and the warm phase adds none. Coverage, 10 TSan runs: 85 to 111 verifications, 30 to 56 concurrent duplicate verifications per run (the race the bitmap allows).
2. **Deterministic windows** (the `LazyPageTesting` hook holds two threads): `publish` (both threads map one segment; the loser returns the winner's bytes; one segment counted), `mark` (both verify one page, both set its bit, later lookups verify nothing), `raced` (a thread parked after its miss while the segment is published, at the pack count and at the cap). Reached in 10 of 10 runs each.
3. **Orderings:** comments at the slot table and at the bit test in `src/detent_local/lazy_page_access.cc`. The slot publish needs release/acquire; the bit does not (immutable bytes), and keeps acquire/release as D3 states.
4. **Corrupted page on first touch** (`corrupt`): one flipped byte in a definition page's header logical digest, which only the hash catches; 16 threads first-touch it at once and all 16 are refused, the generation is failed, and a good page is refused after; serially the trie walk refuses it too.
5. **Cap miss** (`cap`): with the cap one byte short, the last segment is refused, the generation is failed, and an already-verified page is refused after (today's behavior, pinned).
6. **Sanitizers, separate builds:** TSan `build-tsan` and ASan+UBSan `build-asan` (`-fno-sanitize-recover=undefined`): `test_local_lazy_page_bitmap` 10 of 10 clean in each, `test_local_lazy_page_access` clean in each. CI's linux-tsan and linux-asan-ubsan green.
7. **Mutants** (`.scratch/mutants.py`, each restored after):
   - Relaxed slot load and CAS (TSan, full test): killed 5 of 5, "ThreadSanitizer: data race".
   - Verification skipped (`corrupt`): killed 3 of 3.
   - Bit set before verification (`corrupt`): killed 3 of 3 (later threads take the bit and return the corrupt page).
   - Bit never set (`mark`): killed 3 of 3 (re-verification).
   - Raced segment refused, the `043058c` fix reverted (`raced`): killed 3 of 3. The full test kills it in its stress case too.
   - Relaxed bit load and `fetch_or` (TSan): survived 5 of 5, as the ordering comment says: the bit guards immutable bytes.
8. **Existing tests:** full Debug `ctest -j4`, 192 of 192 pass (`test_local_trusted_paths` skips by design). `test_local_lazy_page_access` changes only at its forged-digest assertion, per the re-check's ruling.

## Smoke A/B (functional smoke, not a gate number)

`quiet strict`, 2026-11-03, i9-9980HK macOS, Release, Apple clang. Parent `Detent:7716a58` vs `Detent:c5056c1`. `.scratch/ab/lazy_page_smoke.cc` (in the worktree, not committed): a 26-namespace fixture, `PagedReadState::definition` on a random namespace per call, 500,000 calls per thread, five interleaved rounds. Wall time per call per thread:

| Threads | Parent, ns (5 rounds) | S3, ns (5 rounds) |
| ---: | --- | --- |
| 8 | 42133, 36127, 34136, 34494, 34575 | 458, 420, 473, 487, 452 |
| 1 | 632, 639, 573, 594, 606 | 362, 366, 323, 324, 326 |

No regression; the parent's per-generation mutex serializes eight threads. The fixture fits in cache and is not the 5M store, so this does not stand in for T1; the lead measures that on `main`.

## Next

1. Code review. The lead ratchets T1 to S3's numbers on `main` (RD-10).

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
