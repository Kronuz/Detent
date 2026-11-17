# transport-identity: async-drivers 5c: per-frame identity, the group-open exchange, restart with another L

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status transport-identity`.

## Outcome

The host transport checks identity per frame and opens groups by exchange: a frame is admitted only for a group the host stores, a destination incarnation it holds, and a source node matching the connection's node where membership names it (R7); the group-open exchange carries cluster, group, epoch and profile; a host restarted with another L serves every group from its new loop.

## Acceptance criteria

- A1 The per-frame rule and the group-open exchange's checks (Q7 (a)), each refusal tested with values; a joiner installs and catches up as a learner over shared connections, and a lagging follower catches up (`frame_admission`, its voter-only control failing): tests, reviewed by the security lens
- A2 A host restarted with another L serves every group from its new loop: a test

## Contracts

Provides `consensus:transport`, additive beside `Peers`; consumes consensus-driver's DRIVER.md (consensus:57745e8).

## Decisions

Split from consensus-transport by D106 (the contributor's plan, journal Detent:417daa75 B1). D96, D97, D102.

## May land early

Tests and internal code behind them; nothing on the wire before the review.
