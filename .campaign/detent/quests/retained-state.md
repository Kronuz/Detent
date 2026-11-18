# retained-state: Retained-state envelope in the authority

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status retained-state`. It landed before this
campaign, under the earlier campaign skill; its evidence is its ledger, `docs/status/retained-state.md`.

## Outcome

The authority parses a `retained_state` configuration object, defaulting to the operating limits, and refuses a configuration whose replacement pool cannot fund the retained suffix, with a message naming each term, the shortfall and the field to raise.

## Acceptance criteria

- A1 [artifact] R1 the `retained_state` object is parsed and validated like the other objects; defaults equal the operating defaults: `docs/status/retained-state.md`
- A2 [artifact] R2 the funding check refuses with a message naming each term, the shortfall and the field; the boundary passes: `docs/status/retained-state.md`
- A3 [artifact] R3 the replicated-profile question is answered from the code: `docs/status/retained-state.md`
- A4 [artifact] R4 docs, mechanism note and CHANGELOG with the upgrade note: `docs/status/retained-state.md`
- A5 [artifact] R5 each check has a refusal test with the exact message, a boundary test and a revert check: `docs/status/retained-state.md`
- A6 [artifact] R6 existing authority tests pass unchanged; ASan/UBSan and TSan builds of the authority tests: `docs/status/retained-state.md`

## Contracts

New configuration object `retained_state`; stricter refusal of underfunded replacement pools.

## Decisions

1. Owner ruling D9 adopts the object and the stricter check.
2. Design updated to the code: one funding check on the retained suffix; defaults are the operating defaults, not the library ceilings.

## May land early

None.
