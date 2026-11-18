# transport-feed-scan

## Outcome

`HostTransport::feed` does work proportional to the frames and groups that are ready, not to every group the
host stores. At 10,000 idle groups a host stays responsive: no connection ends at `dead_after` (D109; job
261117-092742-231632 failed 3 of 3 at main's code).

## Acceptance criteria

- A1 The cause is shown and removed: a counting probe shows groups visited per fed frame is constant across 100, 1,000 and 10,000 groups (it grows with the group count at main's code): jobs, `--fails-without`
- A2 At 10,000 groups, the idle-cost run that failed (261117-092742-231632) passes 3 of 3, with no connection ending and every frame delivered: a job
- A3 Ordering and fairness hold: control before bulk, per-group credit, and round robin across ready groups, each tested with values, and TSan passes the groups tests: jobs
- A4 The suite passes, plus CI
- A5 An idle group costs at most 64 B in the host transport again (D112; 253-368 B after transport-identity), or the journal shows, field by field, identity's minimum per group and the idle bench meets it: idle-cost jobs at 1,000 groups.

## Notes

Recommended by transport-idle-cost: per-lane ready lists. Ordered after transport-identity and
transport-groups-ci, which also edit host_transport.h.

## May land early

Nothing.
