# daemon-ready-leader: journal

State: waiting on jobs 261110-233158-fb6902 (native), 261110-233210-4eb6b1 (A3 revert), 261110-233212-05140d (A5 mutant), 261110-233214-7d9360 (A6 revert), 261110-233216-1af7ce (F3 mutant), all at 8c2f1c9; branch pushed, lead to accept
Quest: .campaign/detent/quests/daemon-ready-leader.md (the lead's; never edit it)

## Doing

Landing round under D54/D55. Merged main (Detent:0cff345, dense-pages landed) at 76be485 with no conflicts.
Native failure at f3215b1 (261109-170319-f07393): only the eight 500K/100K coordinator and relay cases
failed (timeouts and "stream truncated" on main's v2 paged path, 69-242 s against the fixtures' limits);
every authority test passed, including lost_quorum and daemon_stop. Cause: D45, the v2 paged path that
dense-pages replaced; the merge brings the replacement, no change in this quest's file set. F3: added the
`no-leader` mode to revert_followup_check.sh (8c2f1c9). F1/F2/N5/N2/N3 were fixed at 82024eb/f3215b1
(see Result and rulings); N1 passed at main (261109-164915-21368f), host load.

## Next

1. On the jobs above: fill the Evidence rows from them; if native shows only authority failures, take the lead's call.

History: jobs at 82024eb: 261109-164903-36a5eb failed only test_authority_daemon_stop (script bug, fixed at f3215b1). Green at f3215b1: fix tests 261109-170314-b99d1f (lost_quorum, daemon_stop, daemon_ready, daemon_alone, daemon), A5 mutant 261109-170316-7c18b2, A6 revert 261109-170317-153a91.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | native 261110-233158-fb6902 (test_authority_daemon_ready: one open right after --daemonize, no retry); earlier green on the fix tests 261109-170314-b99d1f |
| A2 | native 261110-233158-fb6902 (test_authority_daemon_ready samples /v1/health from the first answer, never ready without role 2; test_authority_daemon_alone); mutant 261110-233216-1af7ce (leader_known conjunct dropped, F3) must fail it |
| A3 | revert 261110-233210-4eb6b1 (restores main.cc, service.cc, service.hh from 63ed532, builds detentd, passes iff the test fails on READINESS) |
| A5 | native 261110-233158-fb6902 (test_authority_lost_quorum, scenarios A/B/C); mutant 261110-233212-05140d (bare leader_known) must fail it |
| A6 | native 261110-233158-fb6902 (test_authority_daemon_stop); revert 261110-233214-7d9360 (old main.cc) must fail it |
| A4 | D23 (.campaign/detent/PLAN.md): readiness waits for a leader; implemented as above |

Finding to job map: F1 = test_authority_lost_quorum in 261110-233158-fb6902, killed by 261110-233212-05140d. F2 and N5 = test_authority_daemon_stop in 261110-233158-fb6902, killed by 261110-233214-7d9360. F3 = 261110-233216-1af7ce. N2 and N3 are text edits (nits). N4 kept (D40 shape). N1 = 261109-164915-21368f at main. Pending until the jobs finish; the native gate's 500K/100K cases are D45's.

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
4. A restart of a follower is not needed for F1: `leader_contact` starts at runtime construction and `leader_known` false, so a fresh node is not ready until a RoleChanged names a leader.
5. The contact proof counts any AppendResponse we send, including the rejection of a stale-term append (it carries our higher term, so it cannot be told apart); a deposed leader would refresh the stamp at most until it sees that higher term and steps down. N4 kept: the `peers->addresses.empty()` half of `lone_quorum` is the documented D40 shape.
6. The A6 failure test injects the fault with `chmod 000` on the data directory after the listening line: the open journal fd keeps working but the next storage operation fences the worker (seen: health `fenced:true`, "authority worker fenced").
