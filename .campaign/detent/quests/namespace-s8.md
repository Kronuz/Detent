# namespace-s8: Namespace S8: the relay mirror of blocks, base and ring

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status namespace-s8`.

## Outcome

S8 (`docs/design/namespace-delivery.md` D7): every relay keeps a persisted mirror of each namespace's blocks, base and ring, byte-identical to the authority's, plus the immutable objects, so it answers W6 from its own disk after a restart without re-fetching history.

## Acceptance criteria

One line each: `- A1 <criterion>: <the job, test or check that shows it>`. An untagged criterion
needs a job that passed at the landed code; tag one `[artifact]` (a committed `path`),
`[decision]` (an id in PLAN.md's Decisions table) or `[manual]` (checked by hand) only when a job
cannot show it.

- A1 T4(1), relay side: every block and base on every relay is byte-identical to the voters', also across a relay chain: jobs
- A2 After a relay restart, every answer for an R at or above the cut is unchanged and nothing is re-fetched that the mirror holds: a test with bytes fetched
- A3 Crash matrix at every primitive of the mirror's write and adoption: recovery reaches the uninterrupted mirror: the crash-model job
- A4 A relay adopting an upstream's layout follows the upstream's cut and policy changes: a test
- A5 W8 (from S6, D84): relays read W5/W6 from the authority or an upstream relay and are never members: a test with a relay chain

## Contracts

Consumes `namespace-session`, `namespace-blocks` and `agent-store`.

## Decisions

D53 split namespace-s4-s10; the design is `docs/design/namespace-delivery.md` (reviewed and landed). Each step lands alone with CI green. May start once S4 lands, against S6/S7's contracts; lands after both.

## May land early

The mirror's reader side with tests.
