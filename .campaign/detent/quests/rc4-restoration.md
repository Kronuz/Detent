# rc4-restoration: RC-4: failure and restoration

Risk: critical. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status rc4-restoration`. Planned from `docs/RELEASE_PLAN.md` RC-4, `docs/STAGE6_ARCHITECTURE.md` components 3 and 4, and `docs/RESTORE_ARCHITECTURE.md`;
the criteria are the plan's own, not new ones.

## Outcome

A fixed three-voter authority survives the failures it is specified for and can be restored into a fresh group under a new epoch, with every receipt and resource accounted for.

## Acceptance criteria

- A1 Fresh three-voter restore under a new epoch through validated export and checkpoints and bounded bootstrap transactions; old-epoch requests fenced; agent reset authorized separately (component 3; restore blueprint): jobs
- A2 Fixed-voter failures: leader death with pending writes, minority isolation, majority loss, delayed peers, restart and nonempty snapshot catch-up (component 3): jobs
- A3 Selection and resource conservation under heap, mapping, disk, session, connection, history and pinned-reader pressure, alone then together; receipts and an independent census agree; resources return to the retained-owner baseline (component 4): jobs
- A4 Complete-state and durable recovery checks pass with no unresolved release-blocking defect (RC-4 closure): jobs

## Contracts

To be fixed when the lead splits this quest.

## Decisions

1. Critical: restoring users' data. Owner approval and a `design` review before building.
2. `namespace-s3b` (damaged-pack repair) feeds A4.

## May land early

None until the lead splits it.
