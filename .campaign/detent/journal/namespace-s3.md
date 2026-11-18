# namespace-s3: journal

State: landed
Quest: .campaign/detent/quests/namespace-s3.md (the lead's; never edit it)

History: landed at `Detent:5475baf` on 2026-11-04, before this campaign; recorded with
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
| A1 | `docs/status/namespace-s3.md`: "[x] 1. Restart reads `storage.meta`, the table and the roots and `.read` files against their digests" |
| A2 | `docs/status/namespace-s3.md`: "[x] 2. First-touch checks: every page the agent's update path reads from a base generation is hash-checked" |
| A3 | `docs/status/namespace-s3.md`: "[x] 3. The scrub hashes every pack at `scrub_bytes_per_second`, continuously at that pace" |
| A4 | `docs/status/namespace-s3.md`: "[x] 4. T5 over a 100K store: 100 single-byte flips in random pages." |
| A5 | `docs/status/namespace-s3.md`: "[x] 5. A mutant that skips the first-touch check fails T5; sanitizers in separate builds." |
| A6 | `docs/status/namespace-s3.md`: "Also: CI green on every job (log below); a how-it-works note (`docs/mechanisms/trusted-restart.md`)" |

## Result

Landed at `Detent:5475baf` on 2026-11-04 (fix round `Detent:40019a2`).
100K restart 0.670 to 0.117 s (5.7 times, A/A 0.674 s); 10K ranges overlap, no claim.
CI 41880909373 green on every job, ASan/UBSan and TSan included; `live_record_fuzz` target added.
Reviews: architect `docs/reviews/namespace-s3-architect-2026-11-04.md` (on `review/namespace-s3-architect`, `Detent:2b0b9cb`)
and security `docs/reviews/namespace-s3-security-2026-11-04.md` (`Detent:5f2385b`, 924K fuzz inputs), both APPROVE WITH CONDITIONS, folded.
Docs: `docs/mechanisms/trusted-restart.md`, `docs/perf/trusted-restart.md`.
Left open: none.

## Rulings I made

See Decisions 1.
