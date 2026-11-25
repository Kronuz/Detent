# turn-seal-forget-flake: journal: turn_seal forget_threads' 'group 2 keeps sealing' fails on macOS x86_64 CI

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status turn-seal-forget-flake`.

## Outcome

`journal_turn_seal_forget_threads` failed "group 2 keeps sealing" (turn_seal_test.cc:451) on macos-x86_64 in CI run 44392889900 (journal-group-io-sweep, which changes only Host::group_io; the test drives a TurnDriver alone). The check needs the writer thread to have appended to group 2 within 400 turns; on a slow runner it may not. After this quest the cause is named and the test waits on the condition it checks (group 2 sealed at least once) with a bounded deadline instead of a turn count.

## Acceptance criteria

Three to five, at outcome level: `- A1 <criterion>: <the test that shows it>`. An untagged one
needs a job tagged `--for A1` passing at the landed code, often the suite; tag one `[artifact]`
(a committed `path`), `[decision]` (a PLAN.md Decisions id) or `[manual]` (checked by hand) when no
job can show it.

- A1 The test passes 200 of 200 under a light job with the host loaded, and a mutant that stops group 2's sealer still fails it: jobs.
- A2 The suite passes, plus CI.

## Contracts

None.

## Decisions

Filed 2026-11-18 from journal-group-io-sweep's CI.

## May land early

None.
