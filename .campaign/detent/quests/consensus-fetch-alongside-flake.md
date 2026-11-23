# consensus-fetch-alongside-flake: fetch-alongside-consensus passes on slow macOS runners, or names the defect

Risk: normal (raise to high if a fetch returns wrong bytes). The lead writes this quest; contributors
read it and never edit it. Its machine state (status, branch, checks, reviews) is in `state.json`:
`campaign status consensus-fetch-alongside-flake`.

## Outcome

Reported by Xapiand's lead (msg-272): the transport groups test's fetch-alongside case
(`test/transport/groups_test.cc:2543`) fails on CI macos-x86_64 at main a068ba06 (run
44996244171) and at xapiand's 6e1122c (run 45240308075): "13 of 16 exact", commits beside the
fetches NOT within 2 s (slowest 7,077 ms and 4,410 ms). "exact" counts fetches with no error whose
bytes equal the pattern (line 2522), so an inexact fetch is either an error (a timeout or refusal)
or wrong bytes. Wrong bytes are a transport defect; errors and 4-7 s commits beside 8 MiB fetches
through 1 MiB lane budgets are either a lane starvation defect (D102/D106) or a bound that does not
hold on a slow runner. Find which and fix the cause.

## Acceptance criteria

- A1 The case prints, per inexact fetch, its error or the first differing byte, and the cause is
  named with evidence: a local reproduction under load (strict jobs of at most 10 minutes) or CI
  logs: jobs
- A2 The fix removes the cause, shown by a `--fails-without` job (wrong bytes or starvation: a
  test that fails at main; a host-dependent bound: the case's bound derived from measured host lag,
  as the fetch case's heartbeat bound, consensus:2d42387)
- A3 The case passes 50 runs under 4 burners as strict jobs of at most 10 minutes each, and
  `ci:macos-x86_64` is green
- A4 The suite passes and `ci:ci` is green at the landed commit

## Contracts

None changed unless the cause is in the lane (then docs/mechanisms/rpc-lane.md says what changed).

## Decisions

Lead, 2026-11-23: taken from Xapiand's msg-272; consensus transport is this campaign's.

## May land early

None.

## Tasks

None.
