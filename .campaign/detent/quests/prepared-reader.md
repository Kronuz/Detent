# prepared-reader: Prepared-reader attachment, opt-in

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status prepared-reader`. It landed before this
campaign, in Codex's Stage 6 work, which kept no ledger for it; its evidence is `docs/reviews/prepared-reader-component-2026-10-26.md`.

## Outcome

An application can attach to a prepared generation: attachment enforces every caller corpus bound, shares the mapping account, validates the metadata prefix, and keeps extent-only mappings apart from ownership-bearing ones; publication prepares `.read` before the descriptor.

## Acceptance criteria

- A1 [artifact] Prepared attachment enforces every caller corpus bound before deriving verifier limits (finding 1): `docs/reviews/prepared-reader-component-2026-10-26.md`
- A2 [artifact] Attachment uses the shared mapping account; a new generation does not replenish the allowance (finding 2): `docs/reviews/prepared-reader-component-2026-10-26.md`
- A3 [artifact] Metadata prefix validation precedes reclamation's deletion authority (finding 3): `docs/reviews/prepared-reader-component-2026-10-26.md`
- A4 [artifact] Extent-only and ownership-bearing mappings stay distinct; collection removes `.read` before the payload barrier (extent and lifetime): `docs/reviews/prepared-reader-component-2026-10-26.md`

## Contracts

New on-disk `.read` record and the opt-in prepared attachment.

## Decisions

1. No docs/status ledger: the evidence is the component review.
2. Managed default activation, old-head preparation and shared attachment contexts were outside this acceptance (quest `prepared-activation`).

## May land early

None.
