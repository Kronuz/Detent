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

## Round 2

Commit: Detent:29c32169 (journal Detent:2a0f817a)
Verdict: APPROVE
Date: 2026-11-24

I re-reviewed the whole wait mechanism at 29c32169: the relay's states (syncing, watching, idle
re-wait, backoff), the events that move it (changed, idle, failed, stop, good sync, bad sync), and
the authority's parked-next loop with the seal and `serving()` paths. F1 is fixed, and no new
blocking defect was found.

### Evidence

- The probes ran on a copy of the worktree build. `ninja -n` reported no work to do, so it matches
  29c32169. The copy is in `.scratch/detent/review-relay-mirror-idle-wake-correctness/r2-build/`
  and the logs are in `r2/`.
- `hung_follower.py`: the wait was parked at voter 1 and the voter was SIGSTOPped. The relay served
  the leader's heads **2.57 s** after the publish began, a figure that includes about 1 s of churn.
  Round 1 measured 26.61 s.
- `two_relays_races.py`: **0 misses** in 15 rounds. Both relays made 0 syncs at rest, and the
  authority made 5.3 owner turns/s.
- `seal_race.py`, new: one voter. W1 starts the D136 clock, and W2 comes 1 s - delta later, with
  delta from 0 to 39 ms, so W2's wake and sync straddle the seal entry. Over 80 trials the relay's
  `/v2/history/layouts` body matched the authority's within 21 ms of the check every time:
  **0 misses**. An earlier version that compared `relay_probe` digests was invalid, because the
  probe's own cost grew with the history, and I discarded it.
- I did not read the contributor's jobs, which were all still queued: 2c98e3, 92866f, 7888d0,
  a5cd86, bcf6ff, c5282a, ca4837, 377db8, 265462, 9ee26e and the suite b185e1. The mutants are
  clean: caf12c70 has the relay files at main, e0648da3 has the five fix files at 9337f6c9, and
  a1787553 has service_runtime.cc at main. Each new test fails without its own part of the fix, by
  construction:
  - `test_relay_hung`: a SIGSTOPped voter cannot answer 503, so it needs (a).
  - `test_relay_cut-off`: a raw next with no relay involved, so it needs (b).
  - The reseal check in test_http_upstream and the seal check in test_relay_idle: without the seal
    wake, each waits for a 4 s or 30 s deadline, past its 1.5 s or 5 s bound.

### The two questions

- **Can (b) make relays thrash, in a 503 storm or an election?** No. A refused wait sets
  `Failed`, then `skip`, then a sync gated by `next_sync` (mirror_service.cc:447). The sync's
  history routes are gated by the same `serving()`, so when every voter is unready the sync fails
  and the existing backoff applies (interval doubling to `reconnect_maximum`). That is the same as
  polling against main, where the history routes answered 503. A wait is armed only after a good
  sync, so a refusing voter costs at most one request per interval. A voter still in the quorum
  across a leader change keeps its wait parked (applied index), as the doc table says.
- **Can a 204 on a seal mask a real change?** Not for a relay. Frames win in both answerers: the
  authority's re-plan exports before the 204 check (service_runtime.cc:1620-1637), and the relay
  upstream's `revisit` calls `answer` before `resealed`. When `framing` is busy, a parked next can
  get the early 204 instead of frames, but the relay treats an early 204 as a change and syncs, so
  it reads the frames. An agent re-asks its next and gets the frames then, at the cost of one extra
  request.

The doc's two tables match the code: hung means the deadline is `timeout_ms` with a park of at most
half of it; cut off means 503 and `skip`; idle means a re-wait until `maximum_wait_ms` since the
last good sync, then a sync; a seal means an early 204 and a sync.

### Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | — | fixed | A hung voter holding the wait is read past in about one timeout. A cut-off follower answers 503. | hung_follower 2.57 s; test_relay_hung and test_relay_cut-off (jobs queued) | — |
| R2-1 | no | defect | The idle re-wait is not gated by `interval`. The park is `timeout_ms / 2` (http_upstream.cc:267), and an idle answer re-arms at once (mirror_service.cc:438). With `timeout_ms` below twice `interval_ms`, an idle relay makes more requests than polling did. With `timeout_ms` of 1, which the configuration accepts (configuration.cc:100), the park is 0 ms and the re-waits run back to back. Argued from the code, not run. | Code | Park for at least `interval`, or re-arm no sooner than `next_sync`; reject `timeout_ms < 2 * interval_ms` in the configuration. |
| R2-2 | no | gap | The agents' client maps 503 `not_ready` on a next to `AdmissionFull` (client.cc:51), which `unavailable_reset` does not count (coordinator.cc:32). An agent at a cut-off follower therefore retries that follower instead of rotating. That is no worse than before (it was held there), but (b)'s benefit stops at relays. | Code | Owner's call: map `not_ready` to `Unavailable`, here or in voter-read-freshness (D101). |
| R2-3 | no | nit | `upstream->skip()` after a failed wait (mirror_service.cc:443) carries no load: the sync already tries each source once and rotates on refusal. No test fails without it. test_relay_hung's 2.5 s bound fits a second 1 s timeout. | Code | Drop it, so one mechanism rotates sources, or give it a test. |
| R2-4 | no | gap | A seal applied after a relay's layouts read but before the authority first looks at its new wait is not answered early (`was_parked` is false on that pass, service_runtime.cc:1619). The relay then adopts it at the `maximum_wait_ms` deadline. Argued from the code: seal_race.py swept that timing in 80 trials and hit it 0 times. | seal_race.py 0/80 | None needed now. A rerun of seal_race.py would catch a regression. |
| R2-5 | no | nit | Every seal answers every parked W5 next early, agents included: one extra request per seal per parked reader. | Code | None. |

### Not run

- I read none of the round-2 jobs listed above, including A1 af2c4a and band 3908aa and A2 0afbd5
  and 97c202; all were still queued. I did not rerun test_relay_cut-off locally.
- The agent-side 503 behavior (R2-2) is from the code only.

## Round 3

Commit: Detent:75021884 (journal Detent:65a23582)
Verdict: APPROVE
Date: 2026-11-24

### Scope

I reviewed the diff from 29c32169 to 75021884, which touches 3 source lines:
- `src/detent_delivery/client.cc`: `not_ready` maps to `Unavailable`.
- `mirror_service.cc` `watch_loop`: one wait per interval.
- The tests `test_client_reset`, `failed_wait` and `paced`.

I reran the probes on a copy of the worktree build. `ninja -n` reported no work to do, and the
worktree head 65a23582 differs from 75021884 only in the journal. The copy is in `r3-build/` and
the logs are in `r3/`.

### Results

- `two_relays_races.py`: 0 misses in 15 rounds. The slowest change reached a relay in 0.45 s,
  against 0.87 s at 29c32169. At rest both relays made 0 syncs and the authority made 4.3 turns/s.
- `hung_follower.py`: 1.15 s.
- `seal_race.py`: 0 of 40 trials missed the seal.

### The two questions

- **Does the client mapping change any other status's handling?** No. The change adds one string
  to the `Unavailable` branch (client.cc:47), and every other 503 error and 429 still maps as
  before. `test_client_reset` pins five of them. `not_ready` is produced in two places:
  - A W5 next, at service_runtime.cc:1607. This is the case the change is for.
  - The history routes, at service_runtime.cc:1944. Only the relay reads those, and it does not
    use this mapping.

  `Unavailable` matters in one place, `unavailable_reset` (coordinator.cc:32). A pooled agent
  rotates and yields until it has tried every source in a round, then pauses with
  `next_reconnect_delay` (coordinator.cc:1116-1133). An election in which every voter is not ready
  therefore costs one round per backoff, not a storm. A single-source agent is unchanged: it
  pauses as before.
- **Can pacing delay a real change?** It gates every wait, not only idle re-waits, but it cannot
  delay a wake past the sync gate. The new wait W2 is asked no sooner than `asked + interval`,
  where `asked` is when the previous wait W1 started. W1 returned before the sync S1 it woke, so
  `asked + interval` is before S1's `next_sync`. W2 carries S1's heads as cursors, so a change after
  S1 answers it the moment it lands, and the sync that follows is already held to `next_sync` by
  the existing gate. The race probe shows no added latency. A stop interrupts the pacing wait,
  because `close()` notifies `watch_wake`.

The fails-without mutants are clean one-line reverts, each failing its test for the stated reason
by construction:
- aa5ed3f8: client mapping reverted, so the `not_ready` row fails.
- 079c2f3f: `skip()` removed, so `skips == 1 && seen_skips == 1` fails.
- 3aeced3f: no interval gate, so B is asked every 200 ms, well above `answered <= 4`.

### Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| R2-1, R2-2, R2-3 | — | fixed | Fixed respectively by the pacing, by the mapping, and by `failed_wait`. | Above | — |
| R3-1 | no | gap | The A3 fails-without job 261124-194933-393865 at mutant 4381a94d (the relay files at main, the tests at 75021884) cannot build. `FailingWait` declares `bool wait(...) override` (test_http_upstream.cc:648), and main's `Upstream` has no `wait`. The runner records it as "its build failed", not as fails-without evidence. A3's fails-without still stands through 261124-191402-bcf6ff (caf12c70, tests at 29c32169) and 261124-033951-a94472. | Code; runner.py:594-598 | Cancel 393865 and rely on bcf6ff. |

### Not run

- I read none of these jobs; all were still queued:
  - Tests at 75021884: 93c649, 3fd12d, 4ef9df, 115e1d, 196331.
  - Fails-without: 393865, f72663, 93b926, 4672e0, c8f24b, 0ce6a0, 70f43b, 4047b1, 215053.
  - Measurements: A1 2bfd93; A2 14371c and 702b6d.
  - Suite: 5f884c.
- No agent end-to-end run at a cut-off follower. The mapping and rotation are read from the code.
