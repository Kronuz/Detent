# system-failover-port-reuse: journal

State: waiting on jobs 261122-215002-2b7198 261122-215006-40f218 261122-215011-108797
Quest: .campaign/detent/quests/system-failover-port-reuse.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Fix applied (lead widened the set): test commit d0eb9dec (fails), fix commit 7b3f604a. Jobs
queued; pushed for CI.

## Next

1. When the jobs and CI pass: set State to "ready for review" and fill Result.

## Blocked on

Nothing (the file set was widened by the lead).

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | cause above. Loop (20 kill/restart runs) fails without the fix at d0eb9dec: job 261122-215002-2b7198 (--fails-without); passes at 7b3f604a: job 261122-215006-40f218 |
| A2 | suite job 261122-215011-108797; CI on the pushed branch |

## Result

Not ready.

## Rulings I made

- Fix by a bounded restart retry in the harness, not a server change: the server already sets
  SO_REUSEADDR on both acceptors, so no server change can rebind a port another process holds.
