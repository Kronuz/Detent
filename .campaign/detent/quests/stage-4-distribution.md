# stage-4-distribution: Stage 4: portable distribution and immutable local views

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status stage-4-distribution`. It landed before this
campaign, before the earlier skill kept ledgers; its plan is `docs/VALIDATED_ARCHITECTURE.md` Stage 4 and `docs/IMPLEMENTATION.md` (Stage 4 and its prerequisite).

## Outcome

Authenticated authority delivery over completion-based IO, a bounded snapshot and delta codec, immutable dynamic local images with generation leases, and the managed agent and SDK reading them.

## Acceptance criteria

- A1 [artifact] Prerequisite: authenticated authority and completion-based IO, one consolidated review: `docs/ASYNC_AUTHORITY_ARCHITECTURE.md`, `docs/AUTHORITY_COMPONENT_REVIEW.md`
- A2 [artifact] Portable snapshot and delta schema, bounded codec, immutable local views: `docs/DISTRIBUTION_ARCHITECTURE.md`, `docs/LOCAL_IMAGE_FORMAT.md`
- A3 [artifact] Gate: cross-version decoding, malicious lengths, process-kill publication, native memory and concurrency checks; the managed agent and SDK integration accepted: `docs/DELIVERY_INTEGRATION_ARCHITECTURE.md`
- A4 [artifact] The prerequisite's combined pressure and churn, and whole-service telemetry gates: `docs/AUTHORITY_READINESS.md`

## Contracts

None beyond the documents named in the criteria.

## Decisions

1. History: recorded from the plan's own items and gate; a criterion the record leaves open stays open.

## May land early

None.
