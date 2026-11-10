# namespace-s7: Namespace S7: the agent store, one directory, the table, derived ids, tombstones

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status namespace-s7`.

## Outcome

S7 (`docs/design/namespace-delivery.md` D8, W9-W11): the agent store is one directory with `selected`, `change`, `owner.lock`, the table, derived generation and segment ids published with `linkat` and verified reuse, and tombstones for absent keys as no-ops.

## Acceptance criteria

One line each: `- A1 <criterion>: <the job, test or check that shows it>`. An untagged criterion
needs a job that passed at the landed code; tag one `[artifact]` (a committed `path`),
`[decision]` (an id in PLAN.md's Decisions table) or `[manual]` (checked by hand) only when a job
cannot show it.

- A1 W9: one directory per store with `selected`, `change`, `owner.lock` and the table: tests
- A2 W11: derived ids (layout version, profile, build ordinal), `linkat` publication, and verified reuse of an identical file: tests
- A3 T7: an interruption at every primitive of publication and adoption leaves a store the reader opens correctly: the crash-model job
- A4 Applying S6's three answers yields the same store bytes as a full copy at the same position: a test

## Contracts

Consumes `namespace-session`. Provides `agent-store` (D8's layout), which S8's relay mirror and S9's image share.

## Decisions

D53 split namespace-s4-s10; the design is `docs/design/namespace-delivery.md` (reviewed and landed). Each step lands alone with CI green. May start once dense-pages lands, against S6's contract; lands after S6.

## May land early

Derived ids and the table with tests, before adoption.
