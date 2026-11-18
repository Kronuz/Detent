# relay-snapshot-lifecycle: SC-1: relay snapshot lifecycle with retained bootstrap paths

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status relay-snapshot-lifecycle`. It landed before this
campaign, under the earlier campaign skill; its evidence is its ledger, `docs/status/relay-snapshot-lifecycle.md`.

## Outcome

The relay pins a snapshot and funds a finite retained catch-up path from snapshot B to open-time head C before admitting a session, releasing each retained item on exact acknowledgment, in place of eager invalidation.

## Acceptance criteria

- A1 [artifact] The raw/compressed delayed-consumer regression fails on frozen baseline libraries and passes on the candidate (evidence 1): `docs/status/relay-snapshot-lifecycle.md`
- A2 [artifact] Native tests cover live-history eviction, snapshots during staging, early and wrong acknowledgments, unchanged cursors, admission pressure, expiry with outstanding transport and heap/mapping conservation (evidence 2): `docs/status/relay-snapshot-lifecycle.md`
- A3 [artifact] The full-product suite passes 219/219 (evidence 3): `docs/status/relay-snapshot-lifecycle.md`
- A4 [artifact] The real TLS delayed-consumer test passes for raw and compressed snapshots (evidence 4): `docs/status/relay-snapshot-lifecycle.md`
- A5 [artifact] The code review's condition CR1 is met by that test (evidence 5): `docs/status/relay-snapshot-lifecycle.md`
- A6 [artifact] The 500-session sharing and 100K TLS fixtures and the paged authority-relay-agent-reader process check pass (evidence 6): `docs/status/relay-snapshot-lifecycle.md`
- A7 [artifact] A scoped ASan/UBSan build passes the native and delayed TLS regressions (Integration and sanitizers): `docs/status/relay-snapshot-lifecycle.md`

## Contracts

Relay session admission and retention semantics; wire bytes unchanged.

## Decisions

1. Sustained-load qualification of the retained bootstrap envelope stays open under RC-3.

## May land early

None.
