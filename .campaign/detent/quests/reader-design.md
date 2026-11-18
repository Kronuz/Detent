# reader-design: Reader design: libdetent_reader

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status reader-design`. It landed before this
campaign, under the earlier campaign skill; its evidence is its ledger, `docs/status/reader-design.md`.

## Outcome

`docs/design/reader.md` defines the reader core: a C ABI, lazy attach, the switch, the ring with one freer, fork handlers, the `change` file and build steps S1-S9, revised after an architect review and its re-check.

## Acceptance criteria

- A1 [artifact] The full draft: D1-D9, T1-T6, alternatives, limits, Q1-Q5 and S1-S9, rulings R1-R4 verbatim (Done 1): `docs/status/reader-design.md`
- A2 [artifact] The revision folds the preliminary review: L1-L4, RD-1 to RD-11, C1-C4, Q6 (Done 2): `docs/status/reader-design.md`
- A3 [artifact] The re-check fold: L2, N1, N2 and the D3 corrections (State): `docs/status/reader-design.md`
- A4 [artifact] The lead's calls on Q1-Q6 and RD-5 are recorded (Lead calls): `docs/status/reader-design.md`

## Contracts

Defines the `detent_*` C ABI, D4's `change` file and the reader's fork behavior, built by S1-S6.

## Decisions

1. The ledger sets no done criteria of its own; these four are its recorded deliverables.
2. Q1-Q6 are lead calls the owner may reverse (D26); S7-S9 wait on owner Q5 and Q2.

## May land early

None.
