# relay-mirror-idle-wake review: correctness

Commit: Detent:9337f6c9 (the branch head Detent:57fcf226 adds only the journal)
Verdict: CHANGES
Date: 2026-11-24

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

- Suite 261124-175328-eb494f at 9337f6c9: 219 of 219 passed, test_http_upstream (idle),
  test_relay_idle, test_relay_service, test_authority_frames_service and the system tests among
  them.
- CI 45459969452 at 0ef266b3, which has the same relay code (src/detent_relay and
  include/detent_relay are unchanged from 0ef266b3 to 9337f6c9): linux-tsan passed
  test_http_upstream, so the watcher thread beside the mirror thread on one HttpUpstream is clean
  under TSan. That run failed test_relay_service on three Linux jobs ("relay a fetched again after a
  restart"), which dec62ada addressed in the test (N2). CI 45529046071 at 9337f6c9 was queued.
- 261124-033951-a94472: the idle check fails without the fix for its stated reason ("B synced 37
  times in 1.5 s", test_http_upstream.cc:657).
- The fails-without commits are clean reverts: ccd850ec reverts only the five relay files, and
  35e38250 reverts only service_runtime.cc. Each new test fails at its first check without its fix:
  `idle_syncs <= 1`, or the authority above 60 turns/s.

## What I checked myself

Scripts are in `.scratch/detent/review-relay-mirror-idle-wake-correctness/` (home checkout). I ran
them under `quiet light` because the job queue held more than 30 jobs. Jobs cc64aa and de2d23 are
queued to repeat the hung-voter probe at named commits.

- `hung_follower.py SRC BUILD`: three voters, and one relay reading all three with
  `timeout_ms` 1000 and the default 30 s `maximum_wait_ms`. At rest, lsof finds the voter that
  holds the relay's connection, the probe SIGSTOPs that voter, and the quorum publishes 3 writes.
  The probe measures when the relay serves the leader's heads.
  - Fix, using the worktree build at 9337f6c9 (log `local-fix/hung_follower.log`): wait parked at
    voter 1, relay current after **26.61 s**, with 0 syncs in between.
  - Main, using `local-main/hung_follower.log`: daemons copied to `main-build/` from slot sfpr-main
    2e34a603, whose src/detent_relay, include/detent_relay, detent_distribution and detent_delivery
    equal main e9149575. The relay was reading voter 0 and was current after **1.58 s**, after 1
    failed sync and a rotation.
  - Queued as jobs 261124-180301-cc64aa (9337f6c9) and 261124-180305-de2d23 (5da530c8).
- `two_relays_races.py SRC BUILD` at 9337f6c9 (log `local-fix/two_relays_races.log`): one voter
  and two relays, both parked. At rest the authority made **4.0 owner turns/s** and the relays made
  0 syncs in 3 s. In 15 rounds of 3 writes, with gaps of 0 to 400 ms (so writes land between a sync
  and its park and while the other relay's next is being answered), both relays were current within
  0.03 to 0.37 s every round, with **0 misses**.
- I built the table of events against parked states and read the code paths below.

| Event | Parked state | Outcome | How settled |
|---|---|---|---|
| A publish between the relay's sync and its park | The watch carries the post-sync heads as cursors | The authority plans at arrival and answers at once | Race probe, 0 misses |
| A publish while parked, single voter | `looked == applied` | Storage completion wakes the owner (service_runtime.cc:239), `node.service` applies, and `looked != applied` re-plans (1608) | test_relay_idle, race probe |
| Several parked nexts (relays, agents) | Per-task `looked` | Each task is compared on its own (1636-1644). One `framing` slot defers the others, which count as busy and keep the 1 ms turn | Race probe with 2 relays |
| History not ready, or a restore | `looked = UINT64_MAX` (1536) | Busy, 1 ms turn; re-plans once it is ready | Code |
| Positions not enabled | `looked == applied` | Wakes on the enabling entry, which is an applied index change | Code |
| Snapshot install | applied only moves forward (node.cc:837 gate) | Re-plans | Code |
| Seal or compaction past the cursor | The ring start moves | Answered with frames (a reset or drop frame, frames_export.cc:46) | Code |
| Seal that moves no head | No frame | Adopted at the deadline, up to 30 s | N2, by design |
| Client gone (`delivery.settled`) | Any | The reply pump's `changed` callback wakes the owner (reply_pump.hh:18) | Code |
| Multi-voter | `peers` makes every turn transient | 1 ms turns as before; the change has no effect there today | Code (wake_by) |
| Upstream process stops or restarts | Connection reset | The wait throws, the woken sync fails, and the existing backoff applies | A3 test (A stops and returns) |
| Upstream hangs or partitions while holding the wait | No answer, no reset | **The relay is stale until timeout + wait** | hung_follower: 26.61 s against 1.58 s on main (F1) |
| Wait refused (403, 503, FramesBehind) | Throws at once | One sync per `interval` (polling), with no tight loop | Code: `next_sync` gates (mirror_service.cc:447) |

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | defect | A source that hangs while holding the relay's parked next keeps the relay stale until `timeout_ms` + the wait (40 s at defaults), where polling rotated past it in about one `timeout_ms`. With the wait parked, the mirror thread makes no sync (`synced = !watching && …`, mirror_service.cc:447), so the reading channel never meets the hung source and never rotates. The HTTP deadline is `timeout + parked` (http_upstream.cc:107). A follower that is alive but cut off from its leader holds the wait the same way: W5 nexts are not gated by `serving()`, which gates only the history routes (service_runtime.cc:1929), and its applied index does not move. That partitioned case is argued from the code, not run. This breaks the quest's "no sync-latency regression" and A3's "a failure reconnects with the existing backoff" for multi-source upstreams. | hung_follower.py: fix 26.61 s, main 1.58 s, same topology, 1 s timeout | The owner's choice among: (a) bound the wait by `timeout_ms` on the relay (`std::min(limit, options.timeout)` in `HttpUpstream::wait`), which restores main's detection bound at about one request per 10 s idle; (b) have a voter answer its parked nexts 503 once `serving()` is false, which covers a partition but not a hung process; or (c) both. The test: a system test in which the voter holding a relay's parked next is SIGSTOPped and the quorum publishes, and the relay serves it within about 3 x `timeout_ms`. It is fails-without at 9337f6c9, built from hung_follower.py. |
| N1 | no | gap | A2's rows (3ac6f3, 314fca) are at 80393d76, before dec62ada changed when the authority owner sleeps, which is on the path of a publish to a parked next. The reruns 9c056a and 2b809c were queued. The race probe (0.03 to 0.37 s, single voter) shows no miss but is not A2's strict measurement. | Journal A2 row; job states | Read 9c056a and 2b809c into A2. |
| N2 | no | nit | A seal that moves no head reaches a relay only at the wait's deadline (30 s). It is documented ("The deadline: 204"). It made CI 45459969452 fail test_relay_service until dec62ada added a loop that waits up to 5 x 1.5 s plus a `converged()` (up to 60 s) for it. A downstream serves the previous layout for up to 30 s after the last write; data at the head is current. | relay-mirror-wait.md table; test_relay_service.py:851-858 | Owner's call: keep it, or answer a parked next on a seal past its cursor's ring start. |
| N3 | no | nit | The test_relay_idle docstring says the authority keeps "about 20 a second"; it measures 4.0 (journal and race probe). | tests/detent_relay/test_relay_idle.py:5 | Say "its 250 ms health turns". |
| N4 | no | nit | The watch drops a 200 answer's frames, so a name the upstream holds but the relay cannot adopt yet (created after the latest seal) brings a full answer on every interval until the next seal. This is the journal's ruling of "3 requests per interval instead of 2", noted for its export cost. | mirror_service.cc:381, http_upstream.cc:401 | None needed. |

## Not run

- The partitioned-but-alive follower case in F1. It needs a network partition; SIGSTOP covers the
  hung case only.
- I did not read the contributor's queued jobs: A3 7f5598, fails-without 61b78c, loop turns fef9a7,
  fails-without 3c4319, A1 d0505f, band 1a1358, and A2 9c056a and 2b809c. They were still queued.
- CI at 9337f6c9 (45529046071) was queued. No local TSan run, since sanitizers are CI's.
- Multi-voter with the `peers` 1 ms spin removed (multi-voter-idle-wake). The per-task logic here
  carries over, but that quest's peer wake is its own.
- I did not run the main-side probe at main's own authority build. The relay is byte-identical to
  main; the authority binary predates split-detent.
