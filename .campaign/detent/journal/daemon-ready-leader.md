# daemon-ready-leader: journal

State: waiting on jobs 261109-170314-b99d1f (fix tests), 261109-170316-7c18b2 (A5 revert), 261109-170317-153a91 (A6 revert), 261109-170319-f07393 (native), all at f3215b1
Quest: .campaign/detent/quests/daemon-ready-leader.md (the lead's; never edit it)

## Doing

Fix round for review daemon-ready-leader-correctness (CHANGES) at 82024eb. F1 (D41): health `ready` for a
follower needs `leader_known` and a proof of life within `election_max`; the proof is a RoleChanged
naming the leader or our own `AppendResponse` send (the reply to its append), stamped in the owner loop
(service.cc `leader_alive`). F2/N5: `Service::failure()` returns the runtime's exception text; the
`--daemonize` wait ends on it or on SIGTERM without `ready()` and the parent gets the cause. N2: the
managed quick-start status item 2 is marked resolved. N3: the refusal text names the rule. N4 kept (D40
shape, documented in test_daemon_alone.py). N1: job at main below.

## Next

1. On 261109-170314-b99d1f green: Evidence rows A5, A6 and the fix confirmations F1/F2/N5 take its id; the revert rows take 261109-170316-7c18b2 (A5, F3) and 261109-170317-153a91 (A6).
2. On native 261109-170319-f07393: fill A1/A2 with it. `*-500000` cases are fixture-500k's.

History: jobs at 82024eb: 261109-164903-36a5eb failed only test_authority_daemon_stop (script bug: log not yet created under `set -e`, then zombie parent polled with kill -0; plus main.cc idled after a failed wait); lost_quorum, daemon, daemon_ready, daemon_alone passed there; A5 revert 261109-164904-d4954d passed (mutant killed). Fixed at f3215b1.
N1: 261109-164915-21368f ran test_authority_snapshot_catchup at main (ef001a0) and passed, so the 2 s timeout in 261109-162029-7d42a7 was host load (about 37), not a main defect; I did not change that test.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | native 261109-150943-0c7951 (test_authority_daemon_ready: one open right after --daemonize, no retry) |
| A2 | native 261109-150943-0c7951 (same test: --status "ready":true with "role":2; /v1/health sampled every 10 ms from first answer, never ready without role 2; test_authority_daemon_alone: a lone node of a three-voter set returns from --daemonize in under 10 s and reports "ready":false) |
| A3 | 261109-150944-4403ae (earlier pass c05fdc at dd3aad7; restores main.cc, service.cc, service.hh from 63ed532, builds detentd, runs the test; passes iff the test fails on READINESS; 125 on build failure) |
| A5 | pending: 261109-170314-b99d1f (test_authority_lost_quorum, scenarios A/B/C); revert 261109-170316-7c18b2 |
| A6 | pending: 261109-170314-b99d1f (test_authority_daemon_stop); revert 261109-170317-153a91 |
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
4. A restart of a follower is not needed for F1: `leader_contact` starts at runtime construction and `leader_known` false, so a fresh node is not ready until a RoleChanged names a leader.
5. The contact proof counts any AppendResponse we send, including the rejection of a stale-term append (it carries our higher term, so it cannot be told apart); a deposed leader would refresh the stamp at most until it sees that higher term and steps down. N4 kept: the `peers->addresses.empty()` half of `lone_quorum` is the documented D40 shape.
6. The A6 failure test injects the fault with `chmod 000` on the data directory after the listening line: the open journal fd keeps working but the next storage operation fences the worker (seen: health `fenced:true`, "authority worker fenced").
