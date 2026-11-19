# consensus-faultfs: consensus: tests run on faultfs, not journal's MemoryIO

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status consensus-faultfs`.

## Outcome

D104 step 6-7: consensus's tests that build a journal on `journal::MemoryIO` (blocking_entry, driver_simulation, driver, shared_cluster, shared_worker, transport/peers, waiter, wave_open) run on faultlab's faultfs, as journal's own tests do since journal-faultfs (journal:d90797c, faultlab:a3b71d1). Their power-cut and crash cases use faultfs's crash model; no test includes journal/memory_io.h afterwards.

## Acceptance criteria

Three to five, at outcome level: `- A1 <criterion>: <the test that shows it>`. An untagged one
needs a job tagged `--for A1` passing at the landed code, often the suite; tag one `[artifact]`
(a committed `path`), `[decision]` (a PLAN.md Decisions id) or `[manual]` (checked by hand) when no
job can show it.

- A1 `git grep memory_io` over consensus finds nothing; every converted test passes: jobs.
- A2 Each converted crash or power-cut case still detects what it detected: one --fails-without mutant per crash-model test (driver, shared_worker, wave_open at least) still fails on faultfs: jobs.
- A3 The suite passes, plus CI, with suite time within 1.5x of main's: jobs.

## Contracts

None.

## Decisions

Filed 2026-11-18 after journal-faultfs; lands after consensus-close-fenced, consensus-dns-threads and consensus-worker-release, which touch these tests or CMakeLists.txt.

## May land early

None.
