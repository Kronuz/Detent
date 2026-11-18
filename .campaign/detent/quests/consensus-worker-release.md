# consensus-worker-release: Consensus: a closed row releases its journal group

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status consensus-worker-release`.

## Outcome

`Worker::close()` sets `closing_` and never closes its `Store` (worker.h:179, :1139), so journal never runs `Host::close_group` or `TurnDriver::forget` for a closed row: a created row keeps about 2,398 B in journal, and its id cannot be recovered in the live Host. After this quest a closing Worker closes its Store into the table when it can and otherwise uses journal-reopen-closed's release, so a closed row, clean or fenced, holds nothing in journal and recovers in the live Host. Xapiand's cheap-reopen and pool depend on it.

## Acceptance criteria

Three to five, at outcome level: `- A1 <criterion>: <the test that shows it>`. An untagged one
needs a job tagged `--for A1` passing at the landed code, often the suite; tag one `[artifact]`
(a committed `path`), `[decision]` (a PLAN.md Decisions id) or `[manual]` (checked by hand) when no
job can show it.

- A1 Close then recover the same id through the Driver in one live Host, clean and fenced, with records committed and staged: tests that fail at main.
- A2 Heap per created row returns to its pre-open value after close at 10,000 rows (journal and Driver both): a job.
- A3 A crash during close never loses a committed record and never resurrects a dropped one: a seeded sweep job.
- A4 The suite passes with TSan, plus CI.

## Contracts

Consumes journal's Host release (journal-reopen-closed A7).

## Decisions

Filed by the lead 2026-11-18 from consensus-close-fenced F2 (option b).

## May land early

None.
