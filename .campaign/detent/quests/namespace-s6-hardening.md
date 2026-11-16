# namespace-s6-hardening: S6 hardening: ledger admission, staged sink, drop and unfreezable signals (P1)

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status namespace-s6-hardening`.

## Outcome

v2 gets stricter while v1 still serves every consumer: session codecs and the client admit their memory through a `StorageLedger`, the agent-side sink stages a response's prepared generations and publishes nothing when any fragment is corrupt, a dropped followed namespace is signalled, and a live namespace the voter cannot freeze gets an explicit answer instead of silent omission (P1 of D94).

## Acceptance criteria

- A1 namespace-s6's session codecs and client admit their memory through a `StorageLedger`, not the bare heap (S6 ruling R8): a test that a ledger at its bound refuses an answer
- A2 A two-namespace response with namespace 1 complete in fragment 1 and fragment 2 corrupt leaves the table, `change` words and coordinator cursors unchanged and the ledger at its prior use (design F9): a test plus the early-apply mutant, killed
- A3 A dropped followed namespace is signalled to the session (cutover review F5): a test that a follower learns of the drop
- A4 A live followed namespace the voter cannot freeze gets an explicit answer (503 or a retry frame) and the client keeps its cursor (design F6): tests for both cases
- A5 The tier comment in `include/detent_distribution/history.hh` matches the cutover's F1 rule (msg-50): `include/detent_distribution/history.hh` reviewed by the lead [manual]

## Contracts

Additive on the v2 wire: a drop signal and an unfreezable answer, named in `docs/status/namespace-s6-cutover.md` before landing. v1 unchanged.

## Decisions

D94 (lead, from the design review `reviews/namespace-s6-v1-removal-design.md`): P1 of five ordered landings.

## May land early

Nothing: it changes the wire.
