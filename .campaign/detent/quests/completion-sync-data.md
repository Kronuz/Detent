# completion-sync-data: completion: data-only sync through the IO seam

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status completion-sync-data`.

## Outcome

completion's `IO` seam carries data-only syncs: `IO::sync_data_files(span<File* const>)`, defaulting to `File::sync_data` on each, beside `sync_files`. The executor's async `PrimitiveKind::Sync` and `SyncData` go through the seam (`sync_files` and `sync_data_files`), not `File::sync` directly, so a test host can count or skip every sync, async included. Requested by journal-sync-seam (D113).

## Acceptance criteria

Three to five, at outcome level: `- A1 <criterion>: <the test that shows it>`. An untagged one
needs a job tagged `--for A1` passing at the landed code, often the suite; tag one `[artifact]`
(a committed `path`), `[decision]` (a PLAN.md Decisions id) or `[manual]` (checked by hand) when no
job can show it.

- A1 `IO::sync_data_files` exists with a default that calls `File::sync_data` on each file (fdatasync on Linux, F_FULLFSYNC-equivalent rules as `sync_data` has today): a test with values.
- A2 The executor's Sync and SyncData primitives call the seam: a counting IO sees each async sync, a test failing at main.
- A3 No `File::sync` or `File::sync_data` call outside the seam's default implementations: a grep job.
- A4 The suite passes, plus CI.

## Contracts

Provides `completion:io`: `virtual void IO::sync_data_files(std::span<File* const> files)`, same contract as `sync_files` but data-only. Adding a virtual with a default breaks no backend.

## Decisions

D113 (lead): option A of journal-sync-seam's fork.

## May land early

None.
