# namespace-s6-v1-removal: S6 v1 removal: one cursor, no one-shot or poll mode, agent on v2 sessions

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status namespace-s6-v1-removal`.

## Outcome

v1 is gone: the agent and relays consume namespace-s6's v2 sessions served by namespace-s6-cutover, the one-shot endpoint and poll mode are deleted, the W1 cursor is the only cursor, the old exact-name scope check (`AuthorizedScope`) is deleted, session codecs admit memory through a `StorageLedger`, and the agent's sink stages multi-fragment answers atomically (D84, D43: no compatibility).

## Acceptance criteria

- A1 Q4: the one-shot endpoint, poll mode and v1 sessions are removed with nothing left behind, and the old scope check `AuthorizedScope` is gone: a grep job and the suites
- A2 W1: `src/detent_authority/state.hh`'s cursor and every user of the old cursor (detent_local, detent_agent, detent_distribution, detent_delivery, detent_relay, read_view, testing, benchmarks) use the one type: a grep job and the suites
- A3 The agent and a relay chain run end to end on v2 sessions against a live authority cluster, readers answering what the authority answers: the system suites
- A4 namespace-s6's session codecs and client admit their memory through a `StorageLedger`, not the bare heap (S6 ruling R8): a test that a ledger at its bound refuses an answer
- A5 The authority-fed sink stages a multi-fragment answer and applies nothing when a later fragment is corrupt (namespace-s6 review F3): a test with a corrupt second fragment, plus a mutant applying early, killed
- A6 A dropped followed namespace is signalled to the session, never silently omitted (namespace-s6-cutover review F5): a test that a follower learns of the drop

## Contracts

Consumes `namespace-session` (v2, from namespace-s6-cutover). Removes the v1 wire and the old cursor type; every consumer moves in this landing, since deleting v1 alone breaks the agent and the old relay.

## Decisions

D93 (lead): split from namespace-s6-cutover (msg-49), which lands A1-A3. Open, decided before building by a design review: the agent keeps one table entry named after its scope (D88's interim shape) or holds one entry per followed namespace (D8: a coordinator generation and reader per namespace). The lead leans to D8, since D88 framed the scope-named entry as lasting only until the cutover; the design review settles it with the cost of each.

## May land early

Nothing: it removes a wire.
