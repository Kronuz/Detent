# stage-1-raft-safety: Stage 1: legacy Raft safety corrections

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status stage-1-raft-safety`. It landed before this
campaign, before the earlier skill kept ledgers; its plan is `docs/VALIDATED_ARCHITECTURE.md` Stage 1 and `docs/IMPLEMENTATION.md` "Stage 1: legacy Raft safety".

## Outcome

The legacy Raft in Kronuz/cluster is corrected for the defects the review found, each reproduced first, with Xapiand still compatible.

## Acceptance criteria

- A1 [artifact] Deterministic counterexamples: each failure reproduced before its correction: `docs/IMPLEMENTATION.md`
- A2 [artifact] Isolated corrections in cluster, each reviewed: heartbeat progress, affirmative majority, one vote per term, inherited entries, higher-term and stale traffic, length-decoder overflow, term exhaustion: `docs/IMPLEMENTATION.md`
- A3 [artifact] Gate: the Xapiand compatibility matrix (two- and three-node discovery, replication, distributed search): `docs/IMPLEMENTATION.md`

## Contracts

None beyond the documents named in the criteria.

## Decisions

1. History: recorded from the plan's own items and gate; a criterion the record leaves open stays open.

## May land early

None.
