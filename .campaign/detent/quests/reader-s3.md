# reader-s3: Reader S3: the verified-page bitmap in LazyPageAccess

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status reader-s3`. It landed before this
campaign, under the earlier campaign skill; its evidence is its ledger, `docs/status/reader-s3.md`.

## Outcome

`LazyPageAccess` replaces its per-generation mutex, hash map and LRU with D3's lock-free segment table and per-segment verified bitmap; a page is verified once and a corrupted page is refused on first touch.

## Acceptance criteria

- A1 [artifact] The mutex, hash map and LRU are replaced by the lock-free segment table and verified bitmap: `docs/status/reader-s3.md`
- A2 [artifact] A 16-thread TSan test hammers first touch and repeated lookups: `docs/status/reader-s3.md`
- A3 [artifact] The bitmap orderings are justified in a comment: `docs/status/reader-s3.md`
- A4 [artifact] A relaxed-ordering mutant and a skipped-verification mutant each fail a test: `docs/status/reader-s3.md`
- A5 [artifact] A corrupted page is refused on first touch: `docs/status/reader-s3.md`
- A6 [artifact] ASan+UBSan in a separate build; existing tests pass: `docs/status/reader-s3.md`
- A7 [artifact] A smoke A/B against the parent on an 8-thread scattered lookup under `quiet strict`: `docs/status/reader-s3.md`
- A8 [artifact] CI green: `docs/status/reader-s3.md`

## Contracts

`VerifiedPages` (later shared with S3's first touch); `lru_cache` and `ViewLimits::verification_cache_bytes` removed.

## Decisions

1. Lead ruling: the verification cache limit and `lru_cache` go (greenfield; Q5 drops the heap ledger).
2. A forged digest at a verified offset is served; at an unverified offset it is refused (re-check ruling).

## May land early

None.
