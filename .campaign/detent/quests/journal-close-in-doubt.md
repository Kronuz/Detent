# journal-close-in-doubt: journal: a failed close_group leaves its group closed-in-doubt, never open

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status journal-close-in-doubt`.

## Outcome

When `Host::close_group`'s table commit fails after the shard file was replaced, the host restores the group to open in memory while the file on disk says closed (consensus-worker-release durability review F2). A later directory sync and power cut then recovers it closed and marks it lost, so acknowledged records vanish. After this quest, a failed close leaves the group closed-in-doubt: no new appends are acknowledged until a restart resolves it from disk, or the host fails.

## Acceptance criteria

Three to five, at outcome level: `- A1 <criterion>: <the test that shows it>`. An untagged one
needs a job tagged `--for A1` passing at the landed code, often the suite; tag one `[artifact]`
(a committed `path`), `[decision]` (a PLAN.md Decisions id) or `[manual]` (checked by hand) when no
job can show it.

- A1 <criterion>: <evidence>
- A1 A close_group whose table commit fails after the shard replace never leaves the group appendable: a test that fails at main.
- A2 Crash recovery after a failed close and a later sync loses no record that was acknowledged: a fault-injection sweep job.
- A3 A mutant that restores the group to open is killed.
- A4 The suite passes with TSan, plus CI.

## Contracts

<What this quest provides or consumes (APIs, formats, protocols), stated precisely enough that a
quest depending on it can start before it lands.>

## Decisions

<Decisions made, and by whom; open questions, naming the owner or the lead.>

## May land early

<Internal code behind tests, unless the quest is a format, wire or public API. "None" when
everything waits for the review; `land --upto` refuses then.>
