# journal-open-single-read: An open reads each turn log once

Risk: high (durability: the open's recovery order). The lead writes this quest; contributors read
it and never edit it. Its machine state (status, branch, checks, reviews) is in `state.json`:
`campaign status journal-open-single-read`.

## Outcome

journal-bound-first-open (review F1, `.campaign/detent/reviews/journal-bound-first-open-durability.md`)
made the first open durable by reading every loop's log once in `Host::settle_damage` and again in
each driver's recovery: device calls on an undamaged open go from 287 to 545, and a 192 MiB log
opens in 1.7-2.7 s against about 1.0 s (job 261123-111045-7f9830, noisy, beside light holds). A
hook between `recover` and the open record lets the open store the verdict from the one read the
driver already does, so an open costs what it did before the fix and stays durable at every cut.
Also the review's nits: F2 (the `dropped()` comment states group 2's verdict wrongly and the test
does not assert group 2's values) and F3 (`settle_damage` stores bounds and losses in two commits).

## Acceptance criteria

- A1 Open cost against main and against journal-bound-first-open's landing, strict interleaved
  jobs at named commits with the A/A band: device calls on an undamaged open equal main's, and open
  time on a 192 MiB log is within main's band: jobs
- A2 lost_bound_test `first_open` (17 cuts, four rule-5 layouts, two reopens) and the review's
  second-crash probe pass at the fix, and group 2's values are asserted by value: jobs
- A3 A `--fails-without` mutant that writes the open record before storing the verdict fails A2
- A4 [artifact] `docs/mechanisms/lost-bound.md` says where the verdict is stored and that the open
  reads each log once
- A5 The suite passes and `ci:ci` is green at the landed commit

## Contracts

Journal recovery and the group table; no format change.

## Decisions

Lead, 2026-11-23: a follow-up rather than widening journal-bound-first-open, because the one-read
hook is in turn_log.h, which journal-turn-sector-checks (RFTW0002) is changing now.

## May land early

None.

## Tasks

None.
