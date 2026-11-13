# namespace-s6-cutover: S6 cutover: the authority serves W5/W6, incarnations, one cursor

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status namespace-s6-cutover`.

## Outcome

The authority serves namespace-s6's session endpoint from S5.2's ring, blocks and base, new incarnations arrive as full copies, the one-shot endpoint and poll mode are gone, and the W1 cursor is the only cursor (D84).

## Acceptance criteria

- A1 W5/W6 end to end: the authority's `POST /v2/distribution` answers from S5.2's history, chosen by the cut: tests that check bytes per answer against the authority
- A2 W7: a new incarnation arrives as a full copy under the new cursor: a test
- A3 W13: the authority's grants use namespace-s6's matcher; the old scope check is gone: tests and a grep job
- A4 Q4: the one-shot endpoint and poll mode are removed per D43, with nothing left behind: a grep job
- A5 W1: `src/detent_authority/state.hh`'s cursor and every user of the old cursor (detent_local, detent_agent, read_view, benchmarks) use the one type: a grep job and the suites
- A6 namespace-s6's session codecs and client admit their memory through a `StorageLedger`, not the bare heap (S6 ruling R8): a test that a ledger at its bound refuses an answer
- A7 The authority-fed sink stages a multi-fragment answer and applies nothing when a later fragment is corrupt (namespace-s6 review F3): a test with a corrupt second fragment

## Contracts

Consumes `namespace-session` and `namespace-blocks`. F5 of namespace-s5-2's durability review: an answer can fall in a ring gap with nothing signalling it; the session contract must signal it.

## Decisions

D84. Needs namespace-s6, namespace-s5-2 and okt-followups landed.

## May land early

Nothing: it changes the wire.
