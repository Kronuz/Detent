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
- A5 A checkpoint whose peak (bundle bound plus application cap) exceeds the reservation pool fails loudly with a named error at the checkpoint call that brings that cap (the cap is the call's argument; ruling 2026-11-19), never an admit that waits forever (Xapiand's consensus-sim-loops: `reserve_checkpoint`'s "no" means both not-now and never; seed 3 with a 256 KiB pool blocked every later proposal in the group silently). A test with a pool smaller than one checkpoint's peak gets the error; one at exactly the peak admits: jobs, the first failing without the fix.
- A6 consensus repins journal to its main (at least 4d364f1, journal-reopen-closed) and builds against it: consensus:5923b6c fails on journal 4d364f1 at `sync_data_files` (Xapiand's Track C C2), so the repin carries that API change; the suite at the new pin is the evidence.
- A7 A group whose close_group commit failed stays closed-in-doubt for every Driver over the same live Host until the Host reopens (D128): the mark lives in journal's Host (journal:8c92ed9 or later, journal-close-in-doubt), not in the Driver, and a `second_driver` test that fails at consensus:5de6c78 shows it.
- A4 The suite passes with TSan, plus CI.

## Contracts

Consumes journal's Host release (journal-reopen-closed A7).

## Decisions

Filed by the lead 2026-11-18 from consensus-close-fenced F2 (option b).

## May land early

None.
