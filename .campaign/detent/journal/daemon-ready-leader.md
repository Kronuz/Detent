# daemon-ready-leader: journal

State: waiting on jobs 261109-143405-3aa931 (native), 261109-143406-72d271 (A3 revert)
Quest: .campaign/detent/quests/daemon-ready-leader.md (the lead's; never edit it)

## Doing

Implemented at daemon-ready-leader:6177dc9; waiting on the two jobs.

## Next

1. On both green, fill the Evidence rows with the job ids and mark "ready for review".
2. If the revert job exits 125 or 1 for a build or test reason, fix the script, not the test.

## Blocked on

Nothing. For the lead: a fixed-voter node started alone with `--daemonize` now waits for its
quorum and ends at run_daemon's 30 s startup timeout (before: returned with the listener up). A
sequential `--daemonize` of the voters one by one therefore no longer works; start them together.
No doc in the repo prescribes sequential start; CHANGELOG states it. Say if the wait should be
single-voter only.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | native 261109-143405-3aa931 (test_authority_daemon_ready: one open right after --daemonize, no retry) |
| A2 | native 261109-143405-3aa931 (same test: --status "ready":true with "role":2; /v1/health sampled every 10 ms from first answer, never ready without role 2) |
| A3 | 261109-143406-72d271 (restores main.cc, service.cc, service.hh from 63ed532, builds detentd, runs the test; passes iff the test fails on READINESS; 125 on build failure) |
| A4 | D23 (.campaign/detent/PLAN.md): readiness waits for a leader; implemented as above |

## Result

`Service` tracks the leader from `RoleChanged` actions (node leads, or follows a leader != 0) and
exposes `has_leader()`; health `ready` is `worker.ready() && leader known`, republished at the
change and before `has_leader()` flips, so `--status` never lags the wait. `detentd` waits for
`has_leader()` after `service.start()`, then signals the parent. The "listening" log line moved
before the wait. Quick-start (`QUICKSTART.md`, website `quickstart.md`) drops the retry loop and the
503 sample, per the lead's ruling on decision 2. CHANGELOG entry added. Tests:
`tests/detent_authority/test_daemon_ready.sh` (ctest `test_authority_daemon_ready`) and the revert
script `tests/detent_authority/revert_ready_check.sh`.

## Rulings I made

1. Uniform rule for single and fixed voters: a follower with a known leader counts as ready (the
   consensus library exposes no heard-from-leader accessor, so a follower stays ready until a
   RoleChanged to candidate).
2. The existing `test_daemon.sh` keeps its retry loops: it kills and restarts the daemon without
   --daemonize and is not about readiness.
3. The test samples health rather than injecting a leader-less state; the revert job proves the
   sampling plus the no-retry open catch the old behaviour.
