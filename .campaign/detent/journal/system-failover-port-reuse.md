# system-failover-port-reuse: journal

State: waiting on jobs 261123-030312-eaf7b9 261123-030314-52c787 (diagnostic 261123-030338-7989cb)
Quest: .campaign/detent/quests/system-failover-port-reuse.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

CI run 45120261989 at 05474e28 failed test_system_restart_rebind (60.15 s, no output; it is ours).
Cause: `Authority.restart_member` waited for the restarted member to answer in a loop with no
deadline, and the test printed nothing, so any member that stays alive without answering (a TLS
verification failure, a stalled accept) held it silent until ctest's 60 s kill. Fix 38c47070 (merged
main first, pushed): restart_member raises after its deadline naming the attempt, the last refusal
and the member's output; kill_member bounds its waits; the stand-in daemon handshakes in the handler
thread under a 5 s timeout; the test prints its phases and dumps the tasks at 90 s; ctest TIMEOUT 120.
test_local_disk_store's failure is local-disk-store-lease-flake's.

## Next

1. When jobs 261123-030312-eaf7b9 (48 runs, 8 parallel) and 261123-030314-52c787 (system suite -j8
   x3) pass and CI on 38c47070 is green: set State "ready to land" and fill Result.

## Blocked on

Nothing (the file set was widened by the lead).

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | cause above. Loop (20 kill/restart runs) fails without the fix at d0eb9dec: job 261122-215002-2b7198 (--fails-without); passes at 7b3f604a: job 261122-215006-40f218; bounded test at 38c47070, 48 runs 8 parallel under load: 261123-030312-eaf7b9 |
| A2 | suite at 7b3f604a 261122-215011-108797; at 38c47070, system suite -j8 repeated x3: 261123-030314-52c787; CI on the pushed branch |

## Result

Not ready.

## Rulings I made

- The CI hang is named by mechanism (an unbounded wait), not reproduced; the diagnostic job
  261123-030338-7989cb runs the old test 48x at 8 parallel to see whether it reproduces here.

- Fix by a bounded restart retry in the harness, not a server change: the server already sets
  SO_REUSEADDR on both acceptors, so no server change can rebind a port another process holds.
