# transport-groups: async-drivers 5b: host transport over the connection: Control and Bulk per node, loop routing, per-group credit, control first

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status transport-groups`.

## Outcome

consensus gains the host transport of `docs/design/async-drivers.md` (Detent) Build plan row 5 (Track C D16, D10) over transport-connection, beside `Peers`: one Control and one Bulk connection per peer node, each on one loop, placement `(slot x L) >> 12`, a 16-byte route, frames batched per destination loop and posted once per read, per-group credit, control before bulk, writer round-robin, and the Driver's Sink `Send` wired to it.

## Acceptance criteria

- A1 D16's tests: frames batched by destination loop and posted once per read, per-group credit, control before bulk; two hosts of 4 loops each under TSan; a group with full admission never delays another group's heartbeat (`reader_test`): tests
- A2 A maximal upload (chunk commands at the 1 MiB command and 2 MiB RPC caps, types.h) on the shared connection never delays any group's heartbeat or vote past its election timeout (Xapiand F5): a test, plus a mutant putting bulk ahead of control, killed
- A3 The Driver's Sink `Send` runs over the host transport: a three-voter group elects, commits and survives a peer restart over it: a test

## Contracts

Provides `consensus:transport`, additive beside `Peers`; consumes consensus-driver's DRIVER.md (consensus:57745e8).

## Decisions

Split from consensus-transport by D106 (the contributor's plan, journal Detent:417daa75 B1). D96, D97, D102.

## May land early

Tests and internal code behind them; nothing on the wire before the review.
