# journal-blocking-wake: journal turn driver: the blocking entry sees a cross-thread wake at once

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status journal-blocking-wake`.

## Outcome

The turn driver's blocking entry sees a cross-thread wake only at its next pass (`turn_driver.h:211`; journal-host-guards journal:134, R3), so a waiter can sleep a full pass after work arrives. After this quest a wake from another thread ends the blocking wait at once.

## Acceptance criteria

- A1 A cross-thread wake ends the blocking wait without waiting for the next pass: a test that times out without the fix (--fails-without): jobs.
- A2 The suite, TSan and CI pass: jobs.

## Notes

Filed 2026-11-19 from the untracked-commitment audit (J9).
