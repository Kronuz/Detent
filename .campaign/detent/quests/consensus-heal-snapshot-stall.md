# consensus-heal-snapshot-stall: a peer that already holds the entries waits a full snapshot_timeout after a heal

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status consensus-heal-snapshot-stall`.

## Outcome

Reported by Xapiand's lead (msg-136, consensus-sim-loops at consensus:98298fb): when the leader has checkpointed past a peer's stale match and that peer already holds the entries, the leader's SnapshotNeeded goes unanswered and the peer stalls a full snapshot_timeout (60 s) after a heal before the Core retries from its checkpoint. Not a correctness bug. When this quest lands, such a peer catches up without waiting out the timeout, or the 60 s is shown to be the designed bound and documented. Possibly related to simulation seed 399.

## Acceptance criteria

- A1 The stall is reproduced deterministically in the simulation (seed and schedule named) and its cause named from the Core's state at the stall: jobs.
- A2 After the fix the reproduction catches up within 2 heartbeat intervals, and the test fails without the fix (--fails-without): jobs.
- A3 No safety property regresses: the seeded simulation sweep and the suite pass, with TSan, plus CI: jobs.

## Contracts

No wire change; if one is needed, stop and report (the risk and the review change).

## Decisions

Open: whether the 60 s is a design bound (the lead, on A1's evidence).

## May land early

The reproduction test.
