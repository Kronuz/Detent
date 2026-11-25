# groups-upload-stall: Consensus transport: name groups_upload's 3-4 s stall on macos-x86_64 CI

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status groups-upload-stall`.

## Outcome

`consensus_transport_groups_upload` fails on macos-x86_64 CI (main runs 44130104547, 44214025598, 44216299768): the connection carries almost nothing for 3-4 s while host 2's loops stay responsive. Failures cluster in time and don't reproduce locally (transport-feed-scan's journal, A6). transport-feed-scan lands socket sampling that prints on failure. This quest reads the next failure's samples, names the cause and fixes it.

## Acceptance criteria

Three to five, at outcome level: `- A1 <criterion>: <the test that shows it>`. An untagged one
needs a job tagged `--for A1` passing at the landed code, often the suite; tag one `[artifact]`
(a committed `path`), `[decision]` (a PLAN.md Decisions id) or `[manual]` (checked by hand) when no
job can show it.

- A1 [manual] The cause is named from a CI failure's socket samples (run id quoted in the journal).
- A2 The fix has a test or probe that fails without it, or the cause is shown to be the runner (then the test's bound is set from the samples): jobs.
- A4 `backlog` (groups_test.cc:1620, from transport-feed-scan) holds its merge claim deterministically: on consensus main 42019eb CI run 44357306609 linux-x86_64-gcc it delivered all 18000 frames with 18000 credit frames (no merge) because node 1 read at full speed before host 2 owed more than one record per group; the test must make host 2 hold owed credit across frames (or assert what the run guarantees), and its mutant (no merging) must still fail: jobs.
- A3 The suite passes, plus CI.

## Contracts

None.

## Decisions

Filed by the lead 2026-11-17 from transport-feed-scan A6: no fix without a named cause.

## May land early

A4's test-only fix to `backlog` (and its host-transport.md row), once A4's jobs and CI pass at its commit: consensus main's CI is red on it and Xapiand's branches wait.
