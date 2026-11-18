# rc2-client-performance: RC-2: client performance and updates

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status rc2-client-performance`. Planned from `docs/RELEASE_PLAN.md` RC-2 and its row "Final client attachment", and the lead's plan (M3 row, `Detent:2662aef`);
the criteria are the plan's own, not new ones.

## Outcome

Fresh client reads hold the sub-millisecond resident-data first read repeatably, incremental refresh and retained readers are qualified, and daemon preparation is measured apart from attachment.

## Acceptance criteria

- A1 First-read repeatability: resident-data first reads below 1 ms, 200/200, under `exclusive`, on `main`; the measured limits recorded (RC-2 closure; M3 validation): exclusive jobs
- A2 Incremental refresh qualified (RC-2): jobs
- A3 Retained-reader behavior qualified (RC-2): jobs
- A4 Cold-storage and source-sharing qualification retained (release plan, Final client attachment): jobs
- A5 Daemon startup attribution: the dataset-wide daemon preparation profiled and measured separately from client attachment (RC-2): exclusive jobs
- A6 [decision] The parked reader metadata capsule (D27; `track/reader-capsule`, `track/resource-capsule`) adopted or retired on its 5M acceptance (lead plan M3): a Decisions row
- A7 Every optimization claimed for RC-2 has an attributed cost and a controlled comparison (RC-2 closure): jobs

## Contracts

To be fixed when the lead splits this quest.

## Decisions

1. The sub-millisecond target is unchanged; `dense-pages` A15 and `reader-s6` feed A1.
2. The lead splits A1 to A5 into quests when M3 starts.

## May land early

None until the lead splits it.
