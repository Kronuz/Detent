# namespace-s5-1: Namespace S5.1: per-namespace positions, incarnations, directory stream, enabling entry

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status namespace-s5-1`.

## Outcome

S5.1 (`docs/design/namespace-delivery.md` D3, D5, W3): the authority keeps a position per namespace, starting at each namespace's stored revision, with incarnations, live sizes, the namespace directory stream and the enabling entry, assigned at apply on today's one group (Q5 (d), D3).

## Acceptance criteria

One line each: `- A1 <criterion>: <the job, test or check that shows it>`. An untagged criterion
needs a job that passed at the landed code; tag one `[artifact]` (a committed `path`),
`[decision]` (an id in PLAN.md's Decisions table) or `[manual]` (checked by hand) only when a job
cannot show it.

- A1 Positions start at each namespace's stored revision and advance only with that namespace's changes, also across restart and snapshot install: tests at the landed code
- A2 Incarnations: a namespace deleted and re-created gets a new incarnation, and an old cursor is refused with a full-copy answer (W7): a test
- A3 The directory stream lists every namespace with its position, incarnation and live size, and the enabling entry turns per-namespace positions on for a store written before it: tests
- A4 Every replicated input is canonical: positions, incarnations and sizes are identical on every voter, also with a voter restarted mid-run and one installed from a snapshot: a multi-voter test
- A5 Crash matrix at every primitive of the enabling entry and a position update: recovery reaches the uninterrupted state: the crash-model job
- A6 Xapiand triage #35 (msg-13): deleting a namespace on every replica (tombstone, then reclaim) does not race in-flight writes or readers holding leases: a test, or a recorded deferral to S5.2 with its reason

## Contracts

Provides `namespace-positions`: {cluster, incarnation, position} per namespace (W1, W3), the directory stream and the enabling entry, as the design states. S5.2 and S6 build on exactly these.

## Decisions

D53 split namespace-s4-s10; the design is `docs/design/namespace-delivery.md` (reviewed and landed). Each step lands alone with CI green. Starts beside daemon-ready-leader, which changes 42 lines of `src/detent_authority/service.*`, and lands after it.

## May land early

Tests and internal types with no replicated effect.
