# reader-s3: journal

State: landed
Quest: .campaign/detent/quests/reader-s3.md (the lead's; never edit it)

History: landed at `Detent:132b6bd` on 2026-11-03, before this campaign; recorded with
`campaign land --record --earlier`.

## Doing

Nothing.

## Next

1. Nothing; landed.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | `docs/status/reader-s3.md`: "D3 in `LazyPageAccess` (segment table, verified bitmap, `page_verifications()`, `LazyPageTesting` hook)" |
| A2 | `docs/status/reader-s3.md`: "**16-thread stress** (`stress` in `tests/detent_local/test_lazy_page_bitmap.cc`)" |
| A3 | `docs/status/reader-s3.md`: "**Orderings:** comments at the slot table and at the bit test in `src/detent_local/lazy_page_access.cc`." |
| A4 | `docs/status/reader-s3.md`: "Relaxed slot load and CAS (TSan, full test): killed 5 of 5" |
| A5 | `docs/status/reader-s3.md`: "16 threads first-touch it at once and all 16 are refused" |
| A6 | `docs/status/reader-s3.md`: "full Debug `ctest -j4`, 192 of 192 pass" |
| A7 | `docs/status/reader-s3.md`: "`quiet strict`, 2026-11-03, i9-9980HK macOS, Release, Apple clang. Parent `Detent:7716a58` vs `Detent:c5056c1`." |
| A8 | `docs/status/reader-s3.md`: "CI run 41755940771 (dispatch) green on every job" |

## Result

Landed through `Detent:fc76d98` with its review at `Detent:132b6bd` on 2026-11-03; code head `Detent:e0a209a`.
CI 41755940771 green on every job, TSan and ASan/UBSan included; full Debug ctest 192/192.
Mutants: relaxed slot load and CAS 5/5 killed; skipped verification, early bit, no bit, raced refusal 3/3 each.
Smoke A/B, 8 threads: 34-42 us per call on the parent, 0.42-0.49 us on S3 (functional, not a gate number).
Review: `docs/reviews/reader-s3.md`, APPROVE WITH CONDITIONS; CR1 and CR2 fixed in the fix round.
Docs: `docs/mechanisms/verified-page-bitmap.md`.
Left open: none.

## Rulings I made

The relaxed bit load survives by design: the bit guards immutable bytes.
