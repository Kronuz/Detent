# split-consensus: Every source file in consensus is under 1,000 lines, split by moves only

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status split-consensus`.

## Outcome

Owner rule, relayed by Xapiand's lead (2026-11-21): every source file stays under 1,000 lines; new code never adds a file over it. Files over it in consensus today: host_transport.h 1,608, core.h 1,516, worker.h 1,235, driver.h 1,142 and 7 tests. After this quest each is split into cohesive files by moves only (no behavior change), the build and tests name the new files, and nothing over 1,000 lines remains. Proved with cpp-libraries tools/split_check.py (Xapiand's quest split-check) once it lands, else a diff that shows only moved lines.

## Acceptance criteria

- A1 No source file in the repository is over 1,000 lines: a job that lists every file's line count and prints none over
- A2 The split moves code only: split_check.py (or a moved-lines diff) over the quest's range reports no changed line: a job
- A3 The suite and CI pass

## Contracts

<What this quest provides or consumes (APIs, formats, protocols), stated precisely enough that a
quest depending on it can start before it lands.>

## Decisions

<Decisions made, and by whom; open questions, naming the owner or the lead.>

## May land early

<Internal code behind tests, unless the quest is a format, wire or public API. "None" when
everything waits for the review; `land --upto` refuses then.>

## Tasks

<The steps that build the feature, one agent each: `T1 <what> (files; assignment, tier, effort;
after T0)`. Each merges into the quest branch on its own tests.>
