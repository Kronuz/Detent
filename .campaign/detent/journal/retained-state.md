# retained-state: journal

State: landed
Quest: .campaign/detent/quests/retained-state.md (the lead's; never edit it)

History: landed at `Detent:ebfbcab` on 2026-11-03, before this campaign; recorded with
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
| A1 | `docs/status/retained-state.md`: "[x] R1 `retained_state` object parsed and validated like the other objects, defaults equal the operating defaults" |
| A2 | `docs/status/retained-state.md`: "[x] R2 funding check refuses with a message naming each term, the shortfall and the field to raise; boundary passes" |
| A3 | `docs/status/retained-state.md`: "[x] R3 the replicated-profile question answered from the code" |
| A4 | `docs/status/retained-state.md`: "[x] R4 docs, mechanism note and CHANGELOG with the upgrade note" |
| A5 | `docs/status/retained-state.md`: "[x] R5 each check has a refusal test with the exact message and a boundary test; revert check per behavior" |
| A6 | `docs/status/retained-state.md`: "[x] R6 existing authority tests pass unchanged; ASan/UBSan and TSan builds of the authority tests" |

## Result

Landed `Detent:b0471e9` and docs through `Detent:ebfbcab` on 2026-11-03.
Authority tests 7/7; ASan/UBSan 7/7 and TSan 7/7; seven mutants die; CI 41723121996 green on every job.
R3: the retained values stay local, outside `semantic_profile`, `DTABAT02`, the checkpoint and the fingerprint.
Review: `docs/reviews/retained-state.md`, APPROVE TO LAND, three non-blocking notes.
Docs: `docs/RETAINED_STATE_ARCHITECTURE.md`, `docs/mechanisms/authority-retained-state.md`, `CHANGELOG.md`.
Left open: none.

## Rulings I made

Design vs code 1-3 of the ledger.
