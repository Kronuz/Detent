# consensus-liveness-asan-timeout: consensus_liveness stays well inside its timeout under ASan

Risk: normal (a test's cost). The lead writes this quest; contributors read it and never edit it.
Its machine state is in `state.json`: `campaign status consensus-liveness-asan-timeout`.

## Outcome

consensus CI run 45299975989 (linux-asan-ubsan, consensus-simulation-asan-timeout at
consensus:cbe0522, whose diff does not touch liveness_test.cc): `consensus_liveness` timed out at
180 s. On main's last ten CI runs it passed under ASan in 66.6-168.8 s, so its margin against the
180 s TIMEOUT is as small as 6%. The same shape as consensus-simulation-asan-timeout: find what
dominates its time under ASan (unscaled timeouts, fixed round counts) and cut it, without weakening
what the test checks.

## Acceptance criteria

- A1 Where the time goes, measured: per-case or per-phase times under ASan at main, the dominant
  cost named: a job.
- A2 At the fix, the ASan run takes at most half its TIMEOUT on CI's runner class (CI run times at
  main and at the fix, same runner), and a `--fails-without` job at main shows the slow phase.
- A3 Each check the test made at main is still made at the fix (listed in the Result).
- A4 The suite passes and `ci:ci` is green at the landed commit.

## Contracts

None.

## Decisions

Lead, 2026-11-23: its own quest, after consensus-simulation-asan-timeout, which applies the same
method to the simulation.

## May land early

Not applicable.

## Tasks

None.
