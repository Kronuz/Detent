# daemon-ready-leader review: correctness

Commit: Detent:75297add3e51eaac190a3fdb872db2c6b53bb32e
Verdict: CHANGES
Date: 2026-11-09

APPROVE: lands as is, at this commit's code. CHANGES: lands after each blocking finding is fixed
and confirmed by a job at the fixed commit. REJECT: the approach is wrong; the quest is re-planned.
An APPROVE lists no blocking finding; a CHANGES lists at least one.

## Evidence I relied on

- A3: revert jobs 261109-150944-4403ae (at Detent:88f97af, exit 0) and 261109-162030-7df0dc (at
  Detent:75297ad, exit 0). Both logs show the old readiness failing the new test on its first step:
  `READINESS: open right after --daemonize returned 503 ... outcome_unknown`. The revert restores
  main.cc, service.cc and service.hh together, so it shows that the open step kills the old code.
  It does not show that the health conjunct matters on its own; see "What I checked myself".
- A1/A2 at Detent:88f97af: native 261109-150943-0c7951 has test_authority_daemon_ready,
  test_authority_daemon_alone and test_authority_daemon passing. Its only failures are the seven
  `*-500000` coordinator cases that belong to fixture-500k. The source of the quest's code is
  identical at Detent:75297ad (the merge brings in only main's changes).
- Native 261109-162029-7d42a7 at Detent:75297ad was still running when I wrote this. So far it has
  failed `test_authority_snapshot_catchup` (#44), a 2 s HTTPS read timeout in test_multivoter.py
  `--snapshots` with host load around 37. I did not attribute it. See N1.

## What I checked myself

Scripts are under `.scratch/review-daemon-ready-leader-correctness/` in the home checkout. Job
261109-162839-40b5ff at Detent:75297ad runs `run.sh`, which builds detentd and the TLS fixture and
then runs two probes. It exits 0 only when both pass.

- P1 `lost_quorum.py`: three fresh three-voter clusters run in the foreground, each sampling
  /v1/health over HTTPS every 100 ms for 14 s.
  - A. Kill the leader and one follower. The surviving follower has no leader anywhere, but it
    reported `"ready":true` in every one of its 125 samples, including all 53 samples taken more
    than 8 s (four times election_max) after the kill. Its last sample was
    `{ready:true, role:0, term:1}`. **Fails A2.**
  - B. Kill both followers. The leader steps down through CheckQuorum and reports `ready:false`
    from 2.0 s on. Correct.
  - C. Kill the leader. Both survivors report ready again under a new leader in term 2 (roles 0 and
    2). Correct, so followers do learn a new leader.
- P2: `test_daemon_ready.sh` repeated 20 times in the same job under the shared host load (load
  average 30 to 37). 0 of 20 runs failed, each took 5 to 10 s, and each saw 10 to 29 not-ready
  samples before ready:true with role 2.
- Code read against the consensus pin, consensus:ab05507:
  - `Core::follower(leader)` emits RoleChanged whenever the role or the leader changes, including
    0 to X within the same term.
  - `campaign()` emits leader 0, and `become_leader` emits after the no-op entry persists.
  - `fence()` emits no RoleChanged, but `Worker::ready()` includes `!fenced()`, so health stays
    correct for a fenced node.
  - Before a follower campaigns, `drive()` runs a pre-vote, and the pre-vote leaves `leader_`
    unchanged (core.h:1007). Without a quorum the pre-vote never succeeds, so no RoleChanged ever
    arrives. That is the root cause of F1.
  - Restart: each run builds a fresh Runtime with `leader_known = false`, and the startup loop that
    discards network actions (service.cc:1683) runs before `node.start()`, so it cannot drop a
    RoleChanged.
- Risk 2, from code (main.cc:194-199, daemon.cc:120-130):
  - Bounded: the child polls `has_leader()` every 5 ms, and the parent's 30 s startup timeout
    sends SIGTERM and exits 1 with `daemon startup timed out`.
  - Multi-voter: a three-voter node skips the wait, which test_authority_daemon_alone shows (it
    returns in under 10 s with `"ready":false`).
  - A runtime failure before admission throws out of the Service constructor and reaches the parent
    as `E<what>`, so the exit is 1 with the real cause. A failure after admission but before a
    leader exists does not; see F2.
- Risk 3: the `fixed voter count` check (service.cc:373-375) was already on main (Detent:80cea9c),
  in `load_service_options`, which runs before fork, so the refusal goes to the caller's stderr.
  - A config without `voters` is still accepted: it is the single-voter path that
    test_daemon_ready.sh uses.
  - test_daemon_alone.py asserts that a one-member set is refused.
  - The message itself is terse (N3).
  - Because this check refuses any set under 3, the `peers->addresses.empty()` half of
    `lone_quorum` (main.cc:164) cannot be reached (N4).
- Risk 4:
  - The A1 step runs one `curl --max-time 10` with no retry. Its port comes from the log line that
    is written before the wait.
  - The A2 sampling starts at the first reachable answer, and health is published only after the
    Service constructor returns.
  - No step needs a timing window to fail. Every wait is a deadline (bound_port 30 s, 1000 samples).
- Risk 5: QUICKSTART.md:68-74 and website quickstart.md:101-108 match single-voter behavior.
  CHANGELOG.md:5-12 matches the startup behavior but promises more than the code delivers after
  leader loss (F1). docs/status/quickstart-managed.md:32 still lists the old readiness as blocked
  item 2 (N2).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | defect | A follower whose cluster has lost its leader and quorum keeps reporting `"ready":true` in `--status` and /v1/health indefinitely, which breaks A2 ("never reports ready:true while no leader exists"). The cause: `leader_known` changes only on RoleChanged, and a follower without a quorum never gets past pre-vote, and pre-vote keeps `leader_` (consensus:ab05507 core.h:1007). Journal ruling 1 assumed the follower would reach candidate; with pre-vote it does not. | Probe P1 scenario A, job 261109-162839-40b5ff: 53 of 53 samples after 8 s were ready:true, role 0. service.cc:1756-1759 | Options: (a) in consensus, clear the leader and emit `RoleChanged{Follower, term, 0}` when a follower's election deadline passes and it starts a pre-vote (etcd clears `lead` when it becomes pre-candidate), then bump the pin; (b) in Detent, make a follower's `ready` require contact from the leader within election_max, which needs a last-contact accessor or a hook in the peers layer; (c) the owner narrows A2 and the CHANGELOG to "last known leader" semantics, which keeps the current code. Confirm (a) or (b) by rerunning `run.sh`, which must exit 0. |
| F2 | no | defect | If the runtime fails after admission but before a leader exists (Runtime catch at service.cc:2614-2623), `has_leader()` never turns true and the wait loop never notices. `--daemonize` spins for the full 30 s and then reports `daemon startup timed out`, so the real cause is lost from the caller's output. It does exit non-zero and stays bounded, so risk 2 holds. | main.cc:196-198, daemon.cc:127-130; from code, not probed | Also end the wait when the service has stopped or is fenced, and throw its reason so the parent receives `E<cause>`. |
| F3 | no | gap | A3's revert puts back main.cc and the health conjunct together, and the open step kills it first. No job shows the health sampling (A2) killing a mutant that keeps the `--daemonize` wait and drops only `&& leader_known` from health. From reading the code it would be killed, because the foreground node's role-0 window is about 15 to 29 samples in P2. | revert_ready_check.sh:9; P2 sample counts | Add a second revert or mutant that reverts only service.cc:1597, with the pass condition "fails on READINESS: ready:true without a leader". |
| N1 | no | gap | Native at Detent:75297ad (261109-162029-7d42a7) failed `test_authority_snapshot_catchup` with a 2 s read timeout. That test does not read `ready`, and the quest touches only RoleChanged handling in the multi-voter path. Load was around 37, and I did not attribute the failure. | job 261109-162029-7d42a7 log lines 1461-1506 | The lead reruns it, or runs it at main, before landing. |
| N2 | no | nit | `docs/status/quickstart-managed.md:32` (Blocked on 2) still describes the old readiness behavior. | file:line | Mark it resolved by this quest. |
| N3 | no | nit | The refusal text `fixed voter count` does not state the rule (an odd count from 3 to the voter limit). This predates the quest. | service.cc:373-375 | Name the rule in the message, possibly outside this quest. |
| N4 | no | nit | The `peers->addresses.empty()` half of `lone_quorum` cannot be reached while the loader refuses sets under 3. The test docstring says so. | main.cc:164, test_daemon_alone.py:3-5 | Keep it as the documented D40 shape, or drop it until such a set can load. |
| N5 | no | nit | A SIGTERM delivered to the child during the leader wait leaves the loop through `g_stop` and then calls `ready()`, so the parent reports success for a daemon that is already stopping. | main.cc:196-199 | Call `ready()` only if `has_leader()` holds or the node is not a lone quorum. |

Blocking: yes or no. Kind: defect (wrong behavior), gap (unproven claim or missing test), nit. A
finding that would remove or narrow user-visible behavior offers options, including keeping it,
and is never blocking.

## Not run

- Sanitizer builds. The change adds one atomic flag, set with release and read with acquire, and
  no new threads.
- No probe forced F2's failure after admission. That finding comes from reading the code.
- Linux and the native storage backend: these jobs ran on macOS only, so CI covers the rest.
- A restart from persisted state of a fixed-voter node. I checked it by reading the code only (a
  fresh Runtime starts with `leader_known=false`); the single-voter restart is the same code path.
- I did not attribute N1, and I did not wait for native 261109-162029-7d42a7 to finish.
