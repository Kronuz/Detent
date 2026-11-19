# groups-upload-3s-class: consensus: name groups_upload's unexplained 3-4 s stalls from a sampled CI failure

Risk: normal. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status groups-upload-3s-class`.

## Outcome

groups-upload-stall explained the sampled stall (probe run 44441165298, the reader slow on a loaded runner) but not the seven recorded macos-x86_64 failures: four delivered 0-6 MiB with 3.7-4.0 s gaps, and two ran 1.69x and 2.29x window/rate (44216299768, 44242758901). None had samples. groups_upload now prints a summary line (window, reader rate, window/rate, bound) and socket samples on failure (consensus:c782db1). After this quest the 3-4 s class is named from the first such sampled failure on CI.

## Acceptance criteria

Three to five, at outcome level: `- A1 <criterion>: <the test that shows it>`. An untagged one
needs a job tagged `--for A1` passing at the landed code, often the suite; tag one `[artifact]`
(a committed `path`), `[decision]` (a PLAN.md Decisions id) or `[manual]` (checked by hand) when no
job can show it.

- A1 [manual] The cause is named from a sampled CI failure (run id quoted in the journal).
- A2 A fix with a test that fails without it, or the runner shown as the cause from the samples: jobs.
- A3 The suite passes, plus CI.

## Contracts

None.

## Decisions

Filed 2026-11-18. Blocked until the first sampled groups_upload failure on CI; like coordinator-idle-cause (D89).

## May land early

None.
