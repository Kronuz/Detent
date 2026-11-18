# stage-2-durable-consensus: Stage 2: durable journal, checkpoints and strict consensus

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status stage-2-durable-consensus`. It landed before this
campaign, before the earlier skill kept ledgers; its plan is `docs/VALIDATED_ARCHITECTURE.md` Stage 2 and `docs/IMPLEMENTATION.md` "Stage 2".

## Outcome

An application-independent durable journal and checkpoint interfaces, a deterministic strict consensus core with its Worker, and owned bounded snapshot transfer, in Kronuz/cluster.

## Acceptance criteria

- A1 [artifact] Durable journal and checkpoint interfaces: `docs/IMPLEMENTATION.md`
- A2 [artifact] Deterministic strict consensus core and checkpoint Worker: `docs/IMPLEMENTATION.md`
- A3 [artifact] Gate: crash-prefix, torn-write, failed-sync, partition, duplicate and reordered-message, and recovery invariants: `docs/IMPLEMENTATION.md`
- A4 [artifact] Owned-source, bounded-transfer snapshot component with consolidated Architect GO: `docs/SNAPSHOT_TRANSFER_ARCHITECTURE.md`

## Contracts

None beyond the documents named in the criteria.

## Decisions

1. History: recorded from the plan's own items and gate; a criterion the record leaves open stays open.

## May land early

None.
