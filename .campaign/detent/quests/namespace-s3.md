# namespace-s3: Namespace S3: trusted restart, first touch and the scrub

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status namespace-s3`. It landed before this
campaign, under the earlier campaign skill; its evidence is its ledger, `docs/status/namespace-s3.md`.

## Outcome

A restart trusts prepared generations: it reads `storage.meta`, the table, roots, `.read` and the digest-bound `.live` record with no pack read and no page hash; every base page is hash-checked on first touch; a paced scrub hashes every pack continuously and reports damage.

## Acceptance criteria

- A1 [artifact] 1. Restart reads the metadata against digests with no pack read and no page hash; a counter proves zero page hashes: `docs/status/namespace-s3.md`
- A2 [artifact] 2. Every page the update path reads from a base generation is hash-checked against its parent reference first: `docs/status/namespace-s3.md`
- A3 [artifact] 3. The scrub hashes every pack at `scrub_bytes_per_second`, continuously, its pace proven within 10%: `docs/status/namespace-s3.md`
- A4 [artifact] 4. T5 over a 100K store: 100 byte flips; restart ready; first touch fails with a source failure, never a wrong value; the scrub reports every untouched flip in one pass; a corrupt table or `.read` refuses the store: `docs/status/namespace-s3.md`
- A5 [artifact] 5. A mutant that skips the first-touch check fails T5; sanitizers in separate builds: `docs/status/namespace-s3.md`
- A6 [artifact] Also: CI green on every job; a how-it-works note; restart timed at 10K and 100K, before and after: `docs/status/namespace-s3.md`

## Contracts

New on-disk record `generation-<id>.live` and its decoder; a store without `.live` is refused.

## Decisions

1. Lead rulings: shape A (`.live`), one first-touch accessor on the reader's bitmap, a missing `.live` refuses the store, the scrub as one task on the owner loop.
2. Repair of a damaged pack was left open here; the owner chose to build it now (D10, quest `namespace-s3b`).

## May land early

None.
