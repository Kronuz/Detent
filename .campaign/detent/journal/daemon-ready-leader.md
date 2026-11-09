# daemon-ready-leader: journal

State: waiting on jobs 261109-150943-0c7951 (native), 261109-150944-4403ae (A3 revert)
Quest: .campaign/detent/quests/daemon-ready-leader.md (the lead's; never edit it)

## Doing

Implemented at daemon-ready-leader:88f97af (D40 applied, lone quorum = no other voter); waiting on the two jobs at that head. Jobs 261109-143405-3aa931 and 261109-143406-72d271 (at 6177dc9) were cancelled by the lead. Native 261109-143812-a539df (dd3aad7) failed 7/223, all 500K delivery tests (#77 #81 #89 #99 #101 #129 #130): the pre-existing fixture defect on main that dense-pages fixed (Detent:203f729; its jobs 2489dd/19ca42/747eb5; unlanded), run under host load 34. Not this code; not evidence. A3 revert 261109-143813-c05fdc passed at dd3aad7 (old readiness killed with the 503); rerun below at the new head.

## Next

1. On both green, fill the Evidence rows with the job ids and mark "ready for review".
2. If the revert job exits 125 or 1 for a build or test reason, fix the script, not the test.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | native 261109-150943-0c7951 (test_authority_daemon_ready: one open right after --daemonize, no retry) |
| A2 | native 261109-150943-0c7951 (same test: --status "ready":true with "role":2; /v1/health sampled every 10 ms from first answer, never ready without role 2; test_authority_daemon_alone: a lone node of a three-voter set returns from --daemonize in under 10 s and reports "ready":false) |
| A3 | 261109-150944-4403ae (earlier pass c05fdc at dd3aad7; restores main.cc, service.cc, service.hh from 63ed532, builds detentd, runs the test; passes iff the test fails on READINESS; 125 on build failure) |
| A4 | D23 (.campaign/detent/PLAN.md): readiness waits for a leader; implemented as above |

## Result

`Service` tracks the leader from `RoleChanged` actions (node leads, or follows a leader != 0) and
exposes `has_leader()`; health `ready` is `worker.ready() && leader known`, republished at the
change and before `has_leader()` flips, so `--status` never lags the wait. `detentd` waits for
`has_leader()` after `service.start()` only when the node alone is a quorum (D40: no voter set, or no other voter in it), then signals the parent; with a voter set it returns once the listener is up. The "listening" log line moved
before the wait. Quick-start (`QUICKSTART.md`, website `quickstart.md`) drops the retry loop and the
503 sample, per the lead's ruling on decision 2. CHANGELOG entry added. Tests:
`tests/detent_authority/test_daemon_ready.sh` (ctest `test_authority_daemon_ready`) and the revert
script `tests/detent_authority/revert_ready_check.sh`.

## Rulings I made

0. The config loader refuses a voter set under 3 members (service.cc `fixed voter count`), so a one-member set cannot load and its wait cannot be run; test_daemon_alone.py asserts the refusal. D40 (lead): --daemonize waits only for a lone quorum; health `ready` needs a known leader on every node.
1. A follower with a known leader counts as ready (the
   consensus library exposes no heard-from-leader accessor, so a follower stays ready until a
   RoleChanged to candidate).
2. The existing `test_daemon.sh` keeps its retry loops: it kills and restarts the daemon without
   --daemonize and is not about readiness.
3. The test samples health rather than injecting a leader-less state; the revert job proves the
   sampling plus the no-retry open catch the old behaviour.
