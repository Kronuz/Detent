# relay-service-refused-sync-flake: test_relay_service: relay C never counts a sync failure on a source that refused

Risk: normal (relay mirror, a test). The lead writes this quest; contributors read it and never
edit it. Its machine state is in `state.json`: `campaign status relay-service-refused-sync-flake`.

## Outcome

`test_relay_service` fails at `tests/detent_relay/test_relay_service.py:782` ("relay C failed a
sync on a source that refused") after the mirror logs `/v2/history/file answered 404`: about 5 of
21 runs under `ctest -j8` in system-failover-port-reuse's shards, and in legacy-h's native gate
(261123-175720-145715), neither of which changes the relay. The authority answers 404 when a
layout's file is gone at its latest seal. Find whether the relay should treat that 404 as a refusal
(not a sync failure) or the authority should not answer 404 for it, fix the cause, and keep the
test's check as strict as it is.

## Acceptance criteria

- A1 A reproduction at main that fails the check (a job), with the cause named: which file, and why
  it is gone at the latest seal when the relay asks.
- A2 The fix: the reproduction passes; a `--fails-without` job at main; the check at :782 unchanged.
- A3 50 runs of `test_relay_service` under `ctest -j8` beside the suite's other tests: none fail on
  this check (strict jobs).
- A4 The suite passes and `ci:ci` is green at the landed commit.

## Contracts

The relay mirror's handling of the authority's history answers; no wire format change.

## Decisions

Lead, 2026-11-23: its own quest; port-reuse's main-vs-quest A/B timed out twice, and the failure
shows on branches that do not touch the relay.

## May land early

Not applicable.

## Tasks

None.
