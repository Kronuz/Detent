# consensus-seeds-rate-flake: seeds_rate: a rate check's 90-250 ms gap bound fails on a stalled CI runner

Risk: normal (transport, a test). The lead writes this quest; contributors read it and never edit
it. Its machine state is in `state.json`: `campaign status consensus-seeds-rate-flake`.

## Outcome

`consensus_transport_seeds_rate` aborted on linux-arm64-gcc in CI run 45385145255 at
consensus:0d5f362, a branch that does not touch seeds: `seeds_test.cc:570` asserts
`gap >= 90ms && gap <= 150ms + 100ms` on wall time. Find whether the gap measures the seed source's
rate limit or the runner's scheduling, and make the check measure the rate limit as strictly as it
does now on an unstalled host: the measured host lag, as rpc_host_test does (`Lag`), or a logical
clock. The lower bound (the rate limit itself) stays exact.

## Acceptance criteria

- A1 A reproduction that fails the check at main by stalling the process (a job), and the cause named.
- A2 The fix: the reproduction passes; a `--fails-without` mutant that breaks the rate limit (sends
  sooner than 90 ms) still fails the check.
- A3 50 runs beside 4 burners: none fail (strict jobs).
- A4 The suite passes and `ci:ci` is green at the landed commit.

## Contracts

None changed.

## Decisions

Lead, 2026-11-24: its own quest; seen once in CI. Lands after xapiand:consensus-join-v2, which
holds seeds_test.cc for one unrelated line.

## May land early

Not applicable.

## Tasks

None.
