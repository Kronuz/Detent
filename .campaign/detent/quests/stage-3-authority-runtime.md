# stage-3-authority-runtime: Stage 3: bounded transactions and the authority runtime

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status stage-3-authority-runtime`. It landed before this
campaign, before the earlier skill kept ledgers; its plan is `docs/VALIDATED_ARCHITECTURE.md` Stage 3 and `docs/IMPLEMENTATION.md` "Stage 3".

## Outcome

The bounded authority: incremental immutable state, namespace CAS, generation and sequence receipts, durable Worker recovery and checkpoints, bounded HTTP admission, migration tools and an opt-in single-voter daemon profile.

## Acceptance criteria

- A1 [artifact] Bounded transaction API, deterministic state application, sessions and CAS: `docs/AUTHORITY_ARCHITECTURE.md`
- A2 [artifact] Gate: concurrent histories, retry after timeout, delete and recreate, restart result consistency: `docs/AUTHORITY_ARCHITECTURE.md`
- A3 [artifact] Consolidated Architect source GO for the complete component: `docs/IMPLEMENTATION.md`

## Contracts

None beyond the documents named in the criteria.

## Decisions

1. History: recorded from the plan's own items and gate; a criterion the record leaves open stays open.

## May land early

None.
