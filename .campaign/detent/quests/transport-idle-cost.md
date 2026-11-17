# transport-idle-cost: async-drivers 5d: the host transport's idle cost and hop CPU

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status transport-idle-cost`.

## Outcome

The host transport's idle cost and hop CPU are measured and within budget.

## Acceptance criteria

- A1 Per idle group at most 64 bytes and 0 descriptors, frames or timers; an idle connection end within 48 KB; per host `29 KB + 2P x 41 KB` within 10%: idle-cost jobs
- A2 The hop's CPU per frame and per idle wake, measured on the Linux gate host by `measurement.md` (paired rounds, A/A band): measurement jobs and a results doc under docs/

## Contracts

Provides `consensus:transport`, additive beside `Peers`; consumes consensus-driver's DRIVER.md (consensus:57745e8).

## Decisions

Split from consensus-transport by D106 (the contributor's plan, journal Detent:417daa75 B1). D96, D97, D102.

## May land early

Everything: measurements and docs.
