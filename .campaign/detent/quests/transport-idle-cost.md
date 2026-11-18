# transport-idle-cost: async-drivers 5d: the host transport's idle cost and hop CPU

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status transport-idle-cost`.

## Outcome

The host transport's idle cost and hop CPU are measured and within budget.

## Acceptance criteria

- A1 Per idle group at most `230 + 40P` bytes, P the peer nodes (D112), and 0 descriptors, frames or timers; an idle connection end within 48 KB; per host `52 KB + 2P x 44 KB` within 10% (D108): idle-cost jobs
- A2 The hop's CPU per frame and per idle wake, measured on this Mac, which stands in for the Linux gate host (D111), by `measurement.md` (paired rounds, A/A band): measurement jobs and a results doc under docs/

## Contracts

Provides `consensus:transport`, additive beside `Peers`; consumes consensus-driver's DRIVER.md (consensus:57745e8).

## Decisions

Split from consensus-transport by D106 (the contributor's plan, journal Detent:417daa75 B1). D96, D97, D102.

## May land early

Everything: measurements and docs.
