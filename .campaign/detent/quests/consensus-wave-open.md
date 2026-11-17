# consensus-wave-open: Consensus: 64 groups opened at once never fence storage outside the worker

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status consensus-wave-open`.

## Outcome

Opening many groups at once on one host never fails a worker with "storage fenced outside worker" (worker.h:467 and 671). Xapiand's group-core footprint step hit it with 64 groups opened in one wave on bare consensus rows, no strata::group involved (Xapiand:d5b84cdf4, reproducer `GROUP_FOOTPRINT_NOSYNC=1 GROUP_FOOTPRINT_WAVE=64 build/group_footprint_test 64 0` at strata:ba578486). The quest finds why a store reads as fenced outside its worker during a concurrent open and fixes the cause; the check stays.

## Acceptance criteria

Three to five, at outcome level: `- A1 <criterion>: <the test that shows it>`. An untagged one
needs a job tagged `--for A1` passing at the landed code, often the suite; tag one `[artifact]`
(a committed `path`), `[decision]` (a PLAN.md Decisions id) or `[manual]` (checked by hand) when no
job can show it.

- A1 The cause is named and shown: a consensus test that opens 64 groups in one wave fails without the fix and passes with it, x20 under 4-way load: jobs, with the without-fix run failing on the fence message.
- A2 The fence check still catches a store really fenced outside its worker: an existing or new test passes, and a mutant that removes the check fails it.
- A3 TSan over the wave test and the worker suites: a job.
- A4 [manual] The Xapiand reproducer passes against this branch: the Xapiand lead or the lead runs it and records the run.

## Contracts

None changed: the worker's public API and the fence semantics stay; only the open path changes.

## Decisions

Filed by the lead from the Xapiand lead's group-core footprint findings, 2026-11-17.

## May land early

None: the fix is concurrency on the worker; it waits for the review.
