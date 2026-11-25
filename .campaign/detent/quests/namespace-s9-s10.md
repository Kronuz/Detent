# namespace-s9-s10: Namespace S9-S10: detent-image seeding and the group count

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status namespace-s9-s10`.

## Outcome

S9 (`docs/design/namespace-delivery.md` D9, T6): `detent-image`, read-only adoption, `.lease` at adoption, the ordinal-1 fallback. S10: the group count with per-group producer sessions, once T8 is measured on Linux and strata Track C2 exists. The lead splits it when S7 and S8 land.

## Acceptance criteria

One line each: `- A1 <criterion>: <the job, test or check that shows it>`. An untagged criterion
needs a job that passed at the landed code; tag one `[artifact]` (a committed `path`),
`[decision]` (an id in PLAN.md's Decisions table) or `[manual]` (checked by hand) only when a job
cannot show it.

- A1 To be written at its split: the lead's

## Contracts

Fixed at its split.

## Decisions

D53 split namespace-s4-s10; the design is `docs/design/namespace-delivery.md` (reviewed and landed). Each step lands alone with CI green.

## May land early

None until split.
