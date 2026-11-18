# salvage-followups: Salvage follow-ups: one FNV and accurate docs

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status salvage-followups`. It landed before this
campaign, under the earlier campaign skill; its evidence is its ledger, `docs/status/salvage-followups.md`.

## Outcome

`PagedSegmentMarks::hash` uses the shared `fnv1ah64`, proven value-identical, and the publication-atomicity docs match the code.

## Acceptance criteria

- A1 [artifact] Docs match the code on publication atomicity (Done 1): `docs/status/salvage-followups.md`
- A2 [artifact] One FNV: the shared hash equals the old loop on 25,376 identities, with a frozen-loop test and a dying mutant (Done 2): `docs/status/salvage-followups.md`
- A3 [artifact] CI green on all jobs (Done 3): `docs/status/salvage-followups.md`

## Contracts

None: the hash is an in-memory table slot, not a persisted value.

## Decisions

1. Item 1, the port race at the root, waited on a shared-library API; post-W2 later added the bound-port report.

## May land early

None.
