# journal-group-io-cost: Journal: measure group_io's per-call cost by id count (goal5 O4)

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status journal-group-io-cost`.

## Outcome

Xapiand's goal5-audit O4: the Host's `group_io` sweeps every range on each call. Measure its cost per call at 1,000, 10,000, 100,000 and 1,000,000 ids at journal main (4d364f1), and say whether it grows linearly. A linear result becomes a fix quest before Xapiand's cluster-gate.

## Acceptance criteria

Three to five, at outcome level: `- A1 <criterion>: <the test that shows it>`. An untagged one
needs a job tagged `--for A1` passing at the landed code, often the suite; tag one `[artifact]`
(a committed `path`), `[decision]` (a PLAN.md Decisions id) or `[manual]` (checked by hand) when no
job can show it.

- A1 ns per `group_io` call at 1K, 10K, 100K and 1M ids, median of 5 runs each, at journal:4d364f1, with the fitted growth (slope per id): an `exclusive` job.
- A2 [artifact] `docs/perf/group-io.md` records the numbers, the command and the commit.

## Contracts

None.

## Decisions

Filed 2026-11-18 at the Xapiand lead's request (O4).

## May land early

All of it.
