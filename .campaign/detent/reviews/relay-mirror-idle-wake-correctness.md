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

## Round 4

Commit: Detent:13707337 (Detent:b76ae636 plus a main merge; the branch head Detent:f1ec872d adds only the journal)
Verdict: CHANGES
Date: 2026-11-25

I reviewed the whole relay-mirror wait mechanism again from a fresh read of 13707337, not from the
diff. That covers:
- the parked next and its half-timeout bound;
- how the relay reads each answer (`HttpUpstream::next` and `wait`);
- pacing, the idle re-wait and the deadline sync (`MirrorService::run` and `watch_loop`);
- a voter's 503 `not_ready` and the agent client's mapping of it;
- the seal stamp at the authority (`service_runtime.cc`, `namespace_sessions.cc`) and at a relay
  (`mirror_service.cc`).

I ran as a subagent of the lead, not as a session titled `detent · reviewer relay-mirror-idle-wake
correctness`.

### Evidence I relied on

- 261124-233712-62aab8: test_http_upstream at 13707337 passed. It covers:
  - the three hook steps: a seal before B's watch session opened (66 ms), while B's next was
    parked (218 ms), and between B's sync and its next (99 ms);
  - 0 syncs in 1.5 s at rest, and the deadline sync at 4132 ms;
  - `failed wait`: 1 skip, 2 syncs;
  - `paced`: 3 waits and 0 syncs in 3 s.
- CI 45569989196 (Docs) passed at Detent:410fb793, which adds only the journal to 13707337. CI
  45569989739 was still in progress.

### What I checked myself

Scripts and logs are in `.scratch/detent/review-relay-mirror-idle-wake-correctness/r4/`. The queue
held 60 jobs behind an exclusive measurement, so I ran the probes under `quiet light` on the
worktree's builds. `ninja -n` reported both current at f1ec872d: Release in `.scratch/build`, and
TSan in `.scratch/build-tsan` with CI's flags. The same probes are queued as jobs for the record.

1. **The stamp table.** Every row holds at both upstreams, apart from one doc nit (R4-3).
   - An open stamps the record it creates (service_runtime.cc:1586, mirror_service.cc:193).
   - Every next that reaches an answer passes `restamp` first: service_runtime.cc:1617, before the
     export and before the 204 at :1635; mirror_service.cc:217 and :337.
   - Every `restamp` that returns true ends in an answer in the same visit: a 200 export, a 204, or
     an error that the relay takes as a failure and syncs on.
   - The answers that skip `restamp` are all errors: 503 `not_ready`, `source_behind`, 409, 410
     and `outcome_unknown`. Each one makes the relay sync.
   - The stamp is a field of the session record, so it can neither outlive the record nor exist
     before it.
2. **The reader's open costs one sync, with no loop.**
   - The open reports a change in three cases: at start, after a 410 (http_upstream.cc:269-270,
     :286), and when the watch moves to the reading source (`wait`, :404-407). The sync follows,
     and the next wait parks on the open's stamp.
   - Only a refusal moves the reading source (`refuse`, :93-100): a failed exchange, `rotate` or
     `skip`. The watch therefore moves once per refusal.
   - Pacing (mirror_service.cc:393) and `next_sync` hold both waits and syncs to one per interval
     in every case.
   - 62aab8 shows this: `failed wait` makes 2 syncs, and the relay makes 0 syncs at rest.
3. **The hunt.** It found one miss, R4-1, which I made deterministic and also reproduced with real
   processes. The other races hold:
   - A seal applied in the owner turn before or after a next's first visit is answered in that
     turn or the next (`seal_at` at :1557; the applied-index rescan at :2099-2102).
   - Two relays hold separate sessions and stamps. two_relays_races.py: 0 of 15 rounds missed 3 s.
   - A seal between a relay's sync and its next, CI's case: seal_race.py, 0 of 60 trials missed
     4 s.
   - A watch session cannot expire while watched, because every next uses it. A 410 reopens it
     (item 2).
   - On leader loss, 503 `not_ready` makes the relay skip and sync. hung_follower.py: 1.20 s
     against a 10 s limit.
4. **The 13 stamp mutants.** Each is a one-line replacement, and the code says each fails its own
   step:
   - The `open`, `older`, `answered`, `parked` and `ended` steps fail on the timing or the status
     they check.
   - `busy` gets [204, 204] instead of [204, 409].
   - `looked` starts at UINT64_MAX (service.hh:141), so stamp-older-a and stamp-parked-a separate
     new nexts from parked ones as their names say.
   - stamp-open-u fails the hook step "before B's watch session opened".

   None of these jobs had run, so the step where each one fails is still to read (R4-2).
5. **TSan.** No report in either run:
   - seal_late_park, 2 rounds (r4/logs/seal_late_park-tsan-local.log);
   - test_http_upstream, 3 of 3 passed (r4/logs/test_http_upstream-tsan-local.log).

### R4-1: a seal's late 204 is read as idle

`HttpUpstream::next` starts its clock before it connects. It reads a 204 that comes back within the
park as a change (http_upstream.cc:276-278). The upstream starts the same park later:
- the authority at HTTP receipt (service.cc:571), after the connect, TLS and the request;
- a relay when its mirror thread takes the next (mirror_service.cc:226), after any sync ahead of
  it.

When a seal applies, the upstream restamps the session and answers 204 at once
(service_runtime.cc:1617 and :1635; mirror_service.cc:337 and :351). If that 204 reaches the relay
after the park, counted from its send, the relay reads it as idle and waits again at the same heads
(mirror_service.cc:457). The stamp is already current, so the next wait parks too. The relay serves
the seal only at `maximum_wait_ms` after its last good sync (:485, 30 s by default), or at the next
change of a name it mirrors.

The window is the connect, TLS and request time, plus the upstream's queue, plus the way back. It
recurs every park, and in a chain of relays every hop has its own.

relay-mirror-wait.md:62-64 says that the two 204s "cannot be confused". That holds in one
direction only: a deadline's 204 is never early, but a seal's 204 can be late.

The timing test arrived at Detent:29c32169. Main polls, so the defect is the quest's. The suite's
seal steps cannot catch it: each of their seals follows a write by `seal_after`, so it lands during
the relay's sync or while its next is in flight, never late in a park.

Evidence:
- **Deterministic** (r4/seal_late_park.cc). B waits on relay A with a 1 s park, and B's hook holds
  A's mirror thread for 800 ms inside a sync.
  - A adopted the seal at +1.36 to +1.52 s, after B's park and before A's deadline of +1.80 s.
  - B read A's 204 at +1.39 to +1.54 s as idle, and served the seal at its own 10 s deadline:
    8.33 s and 8.41 s after the seal in Release, 8.37 s and 8.40 s in TSan.
  - The control (the same seal with A not held) took 95 and 244 ms in Release, 223 and 331 ms in
    TSan.
- **Real processes** (r4/seal_window.py, mode `other`): an authority and a relay. The relay mirrors
  "a" and each trial writes "b", so seals land at any phase of the relay's parks.
  - Loopback, `timeout_ms` 200: 2 of 40 seals missed 1.5 s. The first was served 28.8 s after the
    seal.
  - With 600 ms of connection setup through a proxy, `timeout_ms` 2000: 10 of 16 missed 3.3 s. The
    first was served 30.2 s after the seal.
  - Writing "a" instead: 0 of 32 and 0 of 16 missed. The seal follows the write by `seal_after`
    (the same phase lock), so it stays out of the window.
- **At the defaults** (5 s parks), estimated from the measured window rather than run: loopback's
  window of about 5 ms makes about 0.1% of seals late, and a 100 ms round trip about 6%. Each is up
  to 30 s late.

Fix options (the protocol choice is the lead's):
1. Recommended: put the seal on the wire. Every next answer and the open carry the session's stamp
   in a header, and the relay syncs when that stamp is newer than the seal its last sync read.
   This removes the timing test and the open's early return, and agents can ignore seals.
   Greenfield, so no compatibility cost.
2. No wire change: restamp only on answers given when a next arrives. A seal that answers a parked
   next then leaves the stamp older, and the following next answers 204 at once, from its arrival.
   This costs one more sync per seal per relay and one more request per seal per agent session. It
   still misses once setup plus queue reaches the park.
3. Keep the behavior and document a seal delay of up to `maximum_wait_ms`. This is the owner's
   call.

Test: seal_late_park's late case, as a hook step in test_http_upstream, must reach B within 1.5 s.
It fails without the fix at 13707337 (8.3 to 8.4 s). I will rerun seal_window.py `other` at
600 ms on the fix.

### Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| R4-1 | yes | defect | A seal's 204 that reaches the relay later than its park, counted from the send, is read as idle. The upstream has already spent the stamp on it, so the relay serves that seal only at `maximum_wait_ms` (30 s) or at the next change. Details above. | seal_late_park (Release 8.33/8.41 s, TSan 8.37/8.40 s, control under 0.35 s); seal_window `other` (2/40 on loopback, 10/16 at 600 ms setup); http_upstream.cc:276-278, service.cc:571, mirror_service.cc:226 and :457 | Option 1 above (recommended) or 2, or keep it (the owner's call); plus a test step that fails without the fix |
| R4-2 | no | gap | None of the 13 stamp fails-without jobs or the contributor's other round-3 jobs had run. The step where each fails is still to read. | The queue at review time | Read each log for the step named in item 4 |
| R4-3 | no | nit | The stamp table's row "a next parked", column "Close or expiry", is not true of a relay: it erases the record at close (mirror_service.cc:200), and its parked next answers 410 on that turn (:333-335). Only the authority keeps the record until the next ends. | Code | Say so in the row |

Notes (non-blocking):
- An agent's parked next answers once per seal, and the agent asks again at once (coordinator.cc:1057-1067). Under steady writes to any name, an idle agent makes one request per seal, where main made one per wait. The doc states this; option 1 could confine seal answers to watch sessions.
- A watch that moves to another source leaves its old session there until idle expiry (60 s at a relay), one per move.
- A watch refused at its open (403, `admission_full`) moves the reading source once per interval. That is polling's rate, not a loop.

### Not run

- None of the contributor's jobs had run. Queued:
  - test_relay_seal_stamp 86bdee and its 12 fails-without;
  - test_http_upstream's 5 fails-without: f5e430, 7bae55, c02fa6, a95aa5 and b457de;
  - test_client_reset 9a08ad and de1dba;
  - test_relay_idle d27d26, 8c0429 and 4d800e;
  - test_relay_hung c578fd and 9e80bf;
  - test_relay_cut-off 6acdab and 0d0617;
  - A2 494ac5 and d29ae2;
  - suite 0cb2a9.

  A1 fc8eca was running, and CI 45569989739 was in progress.
- My jobs were queued too: 261125-000324-c1acff, 261125-000329-ded940, 261125-000334-fb7bf1 and
  261125-002106-d9444f. Their probes ran locally, as above.
- I built no TSan servers, so the process-level probes ran on Release only.
- I have no deterministic case at an authority upstream. Its window comes from connection setup,
  which the proxy run measures without a hook.
