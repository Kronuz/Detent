# namespace-s4: Namespace S4: no re-validation of a tier own build, hash counters

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status namespace-s4`.

## Outcome

D12.2 (`docs/design/namespace-delivery.md`): no tier re-validates its own build and the relay does not re-hash on recovery; frame SHA-256 stays; hash counters per tier.

## Acceptance criteria

One line each: `- A1 <criterion>: <the job, test or check that shows it>`. An untagged criterion
needs a job that passed at the landed code; tag one `[artifact]` (a committed `path`),
`[decision]` (an id in PLAN.md's Decisions table) or `[manual]` (checked by hand) only when a job
cannot show it.

- A1 S4.1: no tier re-hashes its own build; the relay does not re-hash on recovery; frame SHA-256 is unchanged: tests, native, ASan and TSan checks
- A2 S4.2: per-tier hash counters; at 10K and 100K, bytes hashed per tier are at most 1.2 times bytes built: a strict job at the landed code, with the counters it read
- A3 S4.3: a corrupted pack is still caught downstream (S3's first touch or the scrub): a test that fails with the check removed (a revert job)

## Contracts

Changes no format or wire byte.

## Decisions

D53 split namespace-s4-s10; the design is `docs/design/namespace-delivery.md` (reviewed and landed). Each step lands alone with CI green.

## May land early

Hash counters alone, before S4.1.
