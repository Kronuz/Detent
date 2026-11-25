# relay-delta-shutdown-hang: Relay delta shutdown drains

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status relay-delta-shutdown-hang`.

## Outcome

A relay stopped while it serves or applies a delta drains and exits promptly. Today it hangs on
main too: probe `tests/detent_delivery/relay_delta_shutdown.cc` (namespace-s4's branch, run by
`relay_delta_probe.sh`) timed out at main 6cc26585 (261111-080405-ffc37e) and at namespace-s4's head
(261111-080407-249b55).

## Acceptance criteria

- A1 The cause is named in the journal with the stack or trace that shows it: a job.
- A2 The probe is a ctest test that drains within a few seconds, and fails (times out) without the
  fix: the suite plus a `--fails-without` job on a revert.
- A3 Shutdown during each delta phase (receiving, building, publishing) drains: tests in the suite.

## Contracts

None changed.

## Decisions

- Lead, 2026-11-11: found by namespace-s4; pre-existing, so its own quest after namespace-s4.

## May land early

None.
