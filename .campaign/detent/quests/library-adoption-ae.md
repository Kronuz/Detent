# library-adoption-ae: Stage 6: updated libraries A-E at immutable pins

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status library-adoption-ae`. It landed before this
campaign, before the earlier skill kept ledgers; its plan is `docs/RELEASE_PLAN.md` row "Updated libraries A-E" and `docs/DEPENDENCY_UPGRADES.md`.

## Outcome

Detent pins the compatible A-E closure of its Kronuz libraries, the compact btree layout correction included, keeping the consensus and journal pins until final transport adoption.

## Acceptance criteria

- A1 [artifact] The compatible A-E closure adopted at immutable pins, with the reviewed footprint exception: `docs/DEPENDENCY_UPGRADES.md`
- A2 [artifact] Qualification: the native suite and the relay run pass on the adopted pins: `docs/DEPENDENCY_UPGRADES.md`
- A3 [artifact] Final composed product and profile qualification after the remaining extractions: `docs/RELEASE_PLAN.md`

## Contracts

None beyond the documents named in the criteria.

## Decisions

1. History: recorded from the plan's own items and gate; a criterion the record leaves open stays open.

## May land early

None.
