# faultfs-collect-linear: faultlab: faultfs collect() linear in inodes

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status faultfs-collect-linear`.

## Outcome

`filesystem::collect()` (faultfs.h:477-489 at faultlab:a452a8c) checks every inode against every name, durable name and log entry, and runs on every remove, replace and sync_directory, so a run is cubic: journal's reopen_distinct takes 0.84 s at 100 groups, 2.69 s at 200 and 24.51 s at 400 on faultfs, against 0.26-0.78 s on MemoryIO (job 261118-083844-874284), and the three heap cases cannot finish (journal-faultfs). After this quest collect() gathers the referenced ids once per call and frees what nothing references, linear in names + log + inodes, with unchanged semantics: which inodes survive a crash and which are freed.

## Acceptance criteria

Three to five, at outcome level: `- A1 <criterion>: <the test that shows it>`. An untagged one
needs a job tagged `--for A1` passing at the landed code, often the suite; tag one `[artifact]`
(a committed `path`), `[decision]` (a PLAN.md Decisions id) or `[manual]` (checked by hand) when no
job can show it.

- A1 collect() frees exactly what it freed before: the existing faultfs tests pass, plus a test that builds names, durable names, logged renames and open handles over shared inodes and checks the surviving inode set by value: jobs.
- A2 A mutant that frees an inode still named only durably (or only by a pending log entry) fails A1's test: jobs.
- A3 Cost: collect() over N inodes grows linearly; a test or bench at 1K, 10K and 100K inodes shows time per call within 2x per decade: jobs.
- A4 journal's reopen_distinct, reopen_heap and reopen_churn finish on faultfs at their full counts against this commit (via --ref), light: jobs.

## Contracts

None: faultfs's API is unchanged.

## Decisions

Filed 2026-11-18 from journal-faultfs; faultlab-netlab (Xapiand) does not touch faultfs.h.

## May land early

None.
