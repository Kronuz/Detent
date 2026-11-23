# managed-failover: Stage 6: managed authority failover

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status managed-failover`. It landed before this
campaign, before the earlier skill kept ledgers; its plan is `docs/STAGE6_ARCHITECTURE.md` and `docs/MANAGED_FAILOVER_ARCHITECTURE.md`.

## Outcome

Managed agents select among bounded authority endpoints and revalidate quorum periodically, so a leader change no longer needs external routing.

## Acceptance criteria

- A1 [artifact] Bounded endpoint selection and periodic quorum revalidation: `docs/MANAGED_FAILOVER_ARCHITECTURE.md`
- A2 [artifact] Gate: Architect source GO; native, ASan/UBSan, TSan, Linux and FreeBSD qualification: `benchmarks/managed-failover-qualification.json`

## Contracts

None beyond the documents named in the criteria.

## Decisions

1. History: recorded from the plan's own items and gate; a criterion the record leaves open stays open.

## May land early

None.
