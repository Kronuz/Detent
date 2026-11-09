# stage-0-architecture: Stage 0: contracts, fault model, workload matrix

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status stage-0-architecture`. It landed before this
campaign, before the earlier skill kept ledgers; its plan is `docs/VALIDATED_ARCHITECTURE.md`, "Implementation stages and acceptance gates", Stage 0.

## Outcome

The validated architecture for a durable authority group, bounded transactions, resumable relay distribution and immutable local publication, with its contracts, fault model, workload matrix and dependency inventory, and the stage gates that govern Stages 1-6.

## Acceptance criteria

- A1 [artifact] Contracts, fault model, workload matrix and dependency inventory: `docs/VALIDATED_ARCHITECTURE.md`
- A2 [artifact] Gate: the Architect review resolves the P0 design ambiguities (scalability, edge cases, dependency and state, security and scale): `docs/VALIDATED_ARCHITECTURE.md`

## Contracts

None beyond the documents named in the criteria.

## Decisions

1. History: recorded from the plan's own items and gate; a criterion the record leaves open stays open.

## May land early

None.
