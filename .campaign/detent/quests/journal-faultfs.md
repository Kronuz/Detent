# journal-faultfs: journal: its MemoryIO tests and fuzzers run on faultlab's faultfs (faultlab step 5)

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status journal-faultfs`.

## Outcome

Step 5 of faultlab's migration order (cpp-libraries docs/design/faultlab.md, "Migration order"): journal's tests and fuzzers that crash through `MemoryIO` (include/journal/memory_io.h) run on faultlab's faultfs instead, pinned at faultlab:a452a8c as a test-only dependency (completion's target defined before faultlab is fetched). memory_io.h stays until step 8, after consensus and Detent move. Journal moves first so faultfs's namespace subsets and unsynced truncate surface journal bugs here, not in consensus.

## Acceptance criteria

Three to five, at outcome level: `- A1 <criterion>: <the test that shows it>`. An untagged one
needs a job tagged `--for A1` passing at the landed code, often the suite; tag one `[artifact]`
(a committed `path`), `[decision]` (a PLAN.md Decisions id) or `[manual]` (checked by hand) when no
job can show it.

- A1 No journal test or fuzzer includes memory_io.h: `git grep memory_io.h -- test fuzz` is empty: a job.
- A2 Every crash case keeps or strengthens its assertions on faultfs; the journal maps each old MemoryIO case (seed, pick) to its faultfs one: the suite job.
- A3 A seeded fault is still caught: drop one sync on the turn log's commit path (a mutant) and a faultfs case fails: a job that proves it reached the test.
- A4 Any journal behavior faultfs exposes that MemoryIO hid (a namespace subset, an unsynced truncate, a torn sector) is reported to the lead with a reproducer before it is fixed; a fix outside the test files is its own quest.
- A5 The fuzzers build and run 10 minutes each without a finding: jobs.
- A6 The suite passes, plus CI.

## Contracts

Consumes faultlab:faultfs at faultlab:a452a8c. No journal API change.

## Decisions

Filed by the lead 2026-11-18, replacing completion-faultfs; D104, and the Xapiand lead's ruling that journal goes first.

## May land early

None.
