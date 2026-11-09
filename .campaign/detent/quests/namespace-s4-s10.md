# namespace-s4-s10: Namespace delivery S4-S10

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status namespace-s4-s10`. Planned from `docs/status/namespace-design.md` (S4's done criteria) and `docs/design/namespace-delivery.md` "First steps" S5-S10;
the criteria are the plan's own, not new ones.

## Outcome

Namespace delivery as designed: per-namespace positions, the ring, compacted blocks and base, the session endpoint, the agent store and relay mirror, image seeding and the group count.

## Acceptance criteria

- A1 S4.1: no tier re-validates its own build and the relay does not re-hash on recovery; frame SHA-256 stays: jobs
- A2 S4.2: hash counters per tier; at 10K and 100K, bytes hashed per tier at most 1.2 times bytes built: jobs
- A3 S4.3: a corrupted pack is still caught downstream (S3's first touch or the scrub): a test
- A4 S5: the authority's per-namespace positions, incarnations, live sizes, directory stream and enabling entry (S5.1); the "set limits" entry, layout policy, ring, seals, roll-ups and cut (S5.2): jobs
- A5 S6: W1-W8 and W13, cursor, producers, the session endpoint, frames, incarnations, relays as readers (needs S5 and the Q1 and Q4 calls): jobs
- A6 S7: D8's store, one directory, the table, `change`, derived ids with `linkat` and verified reuse, tombstones (needs S6): jobs
- A7 S8: D7's relay mirror of blocks, base and ring (needs S6, S7): jobs
- A8 S9: D9, `detent-image`, read-only adoption, `.lease` at adoption, the ordinal-1 fallback, T6 (needs S1, S7, Q3): jobs
- A9 S10: the group count with per-group producer sessions (needs T8 on Linux, strata Track C2 D12 and the shared turn log): jobs

## Contracts

To be fixed when the lead splits this quest.

## Decisions

1. Needs `namespace-s1-s2` and `namespace-s3b`. Each step lands alone with CI green (design, First steps); the lead splits them into quests in order.
2. T9, T4(7) and the period-64 wire run (namespace-design Next 2) are measured before S5.

## May land early

None until the lead splits it.
