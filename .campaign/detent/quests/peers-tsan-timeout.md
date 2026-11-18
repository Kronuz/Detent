# peers-tsan-timeout: consensus: transport_peers hangs after several-addresses under TSan

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status peers-tsan-timeout`.

## Outcome

`consensus_transport_peers` timed out at 30 s under linux-tsan in CI run 44379638851 (consensus:f3af7f1, Xapiand's consensus-c3-rows, which touches only Core repair tests): the cases through `several-addresses` printed ok in about 8.5 s total, then nothing until the timeout, so `refused-first` (or the case after it) hung or ran over 21 s under TSan. After this quest the cause is named: a hang in the peer mesh (a defect), or a case whose TSan time exceeds the test's 30 s (then the cases' budget is set from measured TSan times). The test passes under TSan repeatedly.

## Acceptance criteria

Three to five, at outcome level: `- A1 <criterion>: <the test that shows it>`. An untagged one
needs a job tagged `--for A1` passing at the landed code, often the suite; tag one `[artifact]`
(a committed `path`), `[decision]` (a PLAN.md Decisions id) or `[manual]` (checked by hand) when no
job can show it.

- A1 [manual] The cause is named from a TSan reproduction (a job that shows the hang or the per-case times) in the journal.
- A2 If a defect: a test that fails without the fix; if a time budget: per-case TSan times recorded and the timeout set with 2x headroom: jobs.
- A3 `consensus_transport_peers` passes 20 of 20 under a TSan build, plus the suite and CI: jobs.

## Contracts

None.

## Decisions

Filed 2026-11-18 from Xapiand's c3-rows CI; third consensus TSan transport flake beside connection-slow-reader-flake and groups-upload-stall.

## May land early

None.
