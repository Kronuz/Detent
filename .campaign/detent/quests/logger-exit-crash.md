# logger-exit-crash: logger: exit without finish can crash

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status logger-exit-crash`.

## Outcome

A process that exits without calling `finish()` never crashes in the logger. Today `logger_queue`'s exit-without-finish child dies with SIGSEGV when the 200 ms join times out: 3 of 150 runs at logger:67391e6, before logger-on-wheel's changes reached that path (logger-followups journal). The cause is found and fixed, not the timeout widened.

## Acceptance criteria

- A1 The cause, named by the code path that touches freed or torn-down state after the join times out: the journal's row and a reproduction job that fails on main (a stress loop or a forced timeout, not luck)
- A2 The exit-without-finish child passes 500 runs under load: one job
- A3 A test fails without the fix: a job, `--fails-without`
- A4 The suite and TSan pass: jobs
- A5 [artifact] `docs/ROADMAP.md` names no retired library (D72: scheduler; left by logger-followups): `docs/ROADMAP.md`

## Contracts

None.

## Decisions

D86.

## May land early

Nothing.
