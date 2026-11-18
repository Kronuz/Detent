# namespace-s6-cutover: S6 cutover: the authority serves W5/W6, incarnations, one cursor

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status namespace-s6-cutover`.

## Outcome

The authority serves namespace-s6's session endpoint from S5.2's ring, blocks and base, new incarnations arrive as full copies, and every v2 path checks namespace-s6's grants (D84). Removing v1, poll mode and the old cursor is namespace-s6-v1-removal (D93).

## Acceptance criteria

- A1 W5/W6 end to end: the authority's `POST /v2/distribution` answers from S5.2's history, chosen by the cut: tests that check bytes per answer against the authority
- A2 W7: a new incarnation arrives as a full copy under the new cursor: a test
- A3 W13: every v2 path (sessions, `/v2/transactions`, `/v2/read`) checks grants with namespace-s6's matcher: tests and a grep job

## Contracts

Consumes `namespace-session` and `namespace-blocks`. F5 of namespace-s5-2's durability review: an answer can fall in a ring gap with nothing signalling it; the session contract must signal it.

## Decisions

D84; D93 (split, lead). Needs namespace-s6, namespace-s5-2 and okt-followups landed.

## May land early

Nothing: it changes the wire.
