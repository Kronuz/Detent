# consensus-peers-attempts-bound: peers_test:621 reconnect attempts bound fails on macos-x86_64

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status consensus-peers-attempts-bound`.

## Outcome

`consensus_transport_peers` aborted on CI macos-x86_64 at test/transport/peers_test.cc:621, `CHECK failed: attempts >= 4 && attempts <= 2 * (elapsed / 100 + 1)` (run 44650101243, at consensus:a292fd0, a test-only commit that does not touch peers). The bound is set from a named cause, as peers-busy-roundtrip-bound did for :1163, or the check is fixed if it measures the wrong thing.

## Acceptance criteria

- A1 [artifact] The cause, named from samples of a failing or loaded run (which side of the bound failed, the elapsed and attempt counts), is stated at the bound in `test/transport/peers_test.cc` with the probe job ids.
- A2 The case passes 200 runs under load, and a mutant that breaks the backoff (no delay, or a doubled delay) fails the bound (--fails-without): jobs.
- A3 The suite and CI pass: jobs.

## Contracts

None.

## Decisions

No bound without a named cause: the lead.

## May land early

Everything.
