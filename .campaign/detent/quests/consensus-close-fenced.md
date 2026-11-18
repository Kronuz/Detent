# consensus-close-fenced: Consensus: Driver::close frees a fenced or half-opened row fully

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status consensus-close-fenced`.

## Outcome

Xapiand's group-core leaves consensus rows open in two cases: a group fenced then destroyed in a live process, and `strata::group::open()` failing after `Driver::open` succeeded; a reopen then throws "the group is open on this Driver" (driver.h:183). Today `close(id)` drops the Worker but keeps the `Group` node in `groups_` until a reopen of the same id erases it (driver.h:184, 906), and whether close completes on a fenced row (close_step waits for `drained()`, no io, no watch) is untested. After this quest `close(id)` on an open, fenced or half-opened row completes, erases the row so its bytes return (Xapiand counts about 6.8 KB per row), and a later open of the same id succeeds.

## Acceptance criteria

Three to five, at outcome level: `- A1 <criterion>: <the test that shows it>`. An untagged one
needs a job tagged `--for A1` passing at the landed code, often the suite; tag one `[artifact]`
(a committed `path`), `[decision]` (a PLAN.md Decisions id) or `[manual]` (checked by hand) when no
job can show it.

- A1 `close(id)` on a fenced row completes and a reopen of the id succeeds: a test, failing at main if it hangs or throws.
- A2 `close(id)` after a successful open whose caller then fails (no create, no records) completes and frees the row: a test.
- A3 A closed row holds no Driver bytes: the Driver's per-row count returns to its pre-open value after close, at 10,000 rows; heap per created row is reported split into Driver and journal parts, the journal part owned by journal-reopen-closed A7: a job.
- A4 Close with waiters, IO or a watch in flight still completes them as today: the suite and TSan.
- A5 The suite passes, plus CI.
- A6 The Driver never spins after shutdown (msg-71, Xapiand's consensus-sim-loops): `step` re-watching a group that awaits its turn barrier (driver.h:647) after `shutdown_` fires (:942) makes every watch throw `cancelled` at once and re-arm, so the loop never idles until the barrier lands (600,000 wakes seen). Repro: consensus:ecb2a37, `SIM_DRIVER_SEEDS=1 build/consensus_simulation_test 0 1000 2` (seeds 2, 4, 6, 8-10, 14-16, 18-24 of 1-24). After the fix those seeds pass and a test bounds the wakes after shutdown: jobs, fails without.

## Contracts

`Driver::close(id)`: supported on any row that is open, fenced or never created; after it completes the id is free. Xapiand's group-core and catalog rely on this.

## Decisions

Filed by the lead 2026-11-17 on the Xapiand lead's question.

## May land early

None.
