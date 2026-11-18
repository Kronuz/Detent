# namespace-design: Namespace delivery design

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status namespace-design`. It landed before this
campaign, under the earlier campaign skill; its evidence is its ledger, `docs/status/namespace-design.md`.

## Outcome

`docs/design/namespace-delivery.md` defines per-namespace delivery on one store directory: grouped syncs, trusted restart and scrub, compacted-block history cut by cost, the wire list and build steps S1-S10, revised through an architect review, two re-checks, a blocks review and a roll-up review.

## Acceptance criteria

- A1 [artifact] The full draft: D3-D12, strata fit with P1-P3, reader alignment, T1-T8, W1-W12, Q1-Q5, S1-S9; owner rulings D1 and D2 verbatim: `docs/status/namespace-design.md`
- A2 [artifact] The preliminary review folded: L1-L7 and ND-R findings: `docs/status/namespace-design.md`
- A3 [artifact] The first re-check folded: N1-N6: `docs/status/namespace-design.md`
- A4 [artifact] The second re-check folded: N7-N11: `docs/status/namespace-design.md`
- A5 [artifact] The owner's compacted-blocks ruling folded, then the blocks review (NB-L, NB-C) and its re-check (NR-1 to NR-4): `docs/status/namespace-design.md`
- A6 [artifact] The roll-up conditions RU-C1 to RU-C5 folded, with the lead's calls on RU-C3, RU-3 and RU-C4: `docs/status/namespace-design.md`

## Contracts

Defines D11's sync order (built by S1/S2), D10's trusted restart (S3, S3b) and the namespace table (S7).

## Decisions

1. Owner ruling D8 (compacted blocks, cut by cost); lead rulings L1 (one store directory) and C2 (random file ids until L6).
2. Open: alpha from T9, T4(7) and `wire2.out` (PLAN follow-ups); the owner may reverse the lead's calls on Q1, Q3-Q5, B1-B3, RU-C3, RU-3, RU-C4.

## May land early

None.
