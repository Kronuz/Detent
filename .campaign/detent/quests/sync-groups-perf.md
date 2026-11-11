# sync-groups-perf: S1/S2 before and after timing table from main

Risk: low. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status sync-groups-perf`.

## Outcome

`docs/perf/sync-groups.md` gives the restart and cold-bootstrap times at 10K, 100K and 5M before
(`Detent:327ca58`) and after S1/S2, with the A/A spread, from namespace-s1-s2's A9 exclusive jobs.
Its "after" rows come from jobs whose code is the landed S1/S2 code, so every row is measured on
`main`. It changes no code, so its own commit never makes those jobs stale.

## Acceptance criteria

- A1 [artifact] The before and after table, each row naming its exclusive job id, medians and the A/A spread, honest magnitude stated: `docs/perf/sync-groups.md`

## Contracts

None.

## Decisions

1. Split from namespace-s1-s2's A12 (lead, 2026-11-09): a docs commit changes job identity, so a
   perf note written after the timings would make the 14 exclusive A9 jobs stale and force a full
   rerun; writing it in its own quest after S1/S2 lands breaks that cycle.

## May land early

None.
