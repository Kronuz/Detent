# namespace-s6: Namespace S6: cursor, producers, session endpoint, frames, relays as readers

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status namespace-s6`.

## Outcome

S6 (`docs/design/namespace-delivery.md` W1-W8, W13): the cursor, producers, one session endpoint, per-namespace frames (ring entries, the trimmed block, or a base), incarnations, relays as readers, and grants by exact name, prefix or `*`.

## Acceptance criteria

One line each: `- A1 <criterion>: <the job, test or check that shows it>`. An untagged criterion
needs a job that passed at the landed code; tag one `[artifact]` (a committed `path`),
`[decision]` (an id in PLAN.md's Decisions table) or `[manual]` (checked by hand) only when a job
cannot show it.

- A1 W5/W6: the session request carries `[{name, cursor}]` or `all` with a directory cursor; frames are ring entries after R, the block holding R + 1 trimmed above R, or a base, chosen by the cut over a history interface S5.2 implements: tests against a test history source that check bytes per answer
- A2 W1: one cursor type in `include/detent/cursor.hh`; `agent_store.hh`'s copy is gone and uses it: a grep job and the suites
- A3 The agent's client side speaks W5/W6 against a test server: a test
- A4 W4: producer sessions, contiguous sequences and `/v2/result` are unchanged on one group: the existing suites pass
- A5 W13: grants by exact names, whole-component prefixes and `*`: the matcher's tests

## Contracts

Consumes `namespace-positions` and `namespace-blocks`. Provides `namespace-session`, the W5/W6 wire, which S7 and S8 build on.

## Decisions

D84 split S6: the authority's endpoint, W7 incarnations, Q4's removal and the old cursor's deletion are namespace-s6-cutover; W8 relays as readers is namespace-s8 A5. D53 split namespace-s4-s10; the design is `docs/design/namespace-delivery.md` (reviewed and landed). Each step lands alone with CI green. May start once S5.1 lands; lands before S5.2, which implements its history interface.

## May land early

Cursor and frame types with tests, before the endpoint.
