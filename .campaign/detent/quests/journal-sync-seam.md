# journal-sync-seam: Journal: every sync through the IO seam

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status journal-sync-seam`.

## Outcome

Every durable sync in journal goes through the IO seam, so a test host can skip or count them. Today the host-table commit and the turn flush call `PosixFile::sync` directly; Xapiand measured them at 38% and 15% of a 289 ms group open with the seam's sync skipped (Xapiand:d5b84cdf4).

## Acceptance criteria

Three to five, at outcome level: `- A1 <criterion>: <the test that shows it>`. An untagged one
needs a job tagged `--for A1` passing at the landed code, often the suite; tag one `[artifact]`
(a committed `path`), `[decision]` (a PLAN.md Decisions id) or `[manual]` (checked by hand) when no
job can show it.

- A1 No direct `PosixFile::sync` (or fsync/fdatasync) call outside the seam's implementation: a grep job.
- A2 A test host that counts syncs sees the host-table commit and the turn flush, with the counts the crash model expects: a test, failing on main.
- A3 The crash-point suites keep their outcomes: the suite.
- A4 A group open's cost with syncs skipped, before and after, on the Xapiand footprint reproducer or a journal equivalent: a strict job and a line in the journal's perf doc.

## Contracts

journal's IO seam gains no new method if an existing one covers sync; if it needs one, the quest file names it before code (Xapiand consumes the seam).

## Decisions

Filed by the lead from the Xapiand lead's group-core footprint findings, 2026-11-17.

## May land early

None.
