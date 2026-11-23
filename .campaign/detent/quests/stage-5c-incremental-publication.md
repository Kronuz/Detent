# stage-5c-incremental-publication: Stage 5C: incremental local publication

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status stage-5c-incremental-publication`. It landed before this
campaign, before the earlier skill kept ledgers; its plan is `docs/VALIDATED_ARCHITECTURE.md` Stage 5 and `docs/INCREMENTAL_PUBLICATION_ARCHITECTURE.md`.

## Outcome

Paged local publication: canonical paged snapshots, warm recovery, shared-segment collection, verified migration and bounded maintenance, opt-in through agent and relay configuration.

## Acceptance criteria

- A1 [artifact] Incremental paged publication, warm recovery, shared-segment collection, migration and maintenance: `docs/INCREMENTAL_PUBLICATION_ARCHITECTURE.md`
- A2 [artifact] Gate: component acceptance with the affected native, platform and sanitizer qualification: `docs/IMPLEMENTATION.md`

## Contracts

None beyond the documents named in the criteria.

## Decisions

1. History: recorded from the plan's own items and gate; a criterion the record leaves open stays open.

## May land early

None.
