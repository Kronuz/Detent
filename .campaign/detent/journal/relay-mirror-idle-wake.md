# relay-mirror-idle-wake: journal

State: blocked on lead: D163's file set (the next request's field lives outside it) and the agents' stamp
Quest: .campaign/detent/quests/relay-mirror-idle-wake.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

### Round 4: D163, the seal stamp on the wire (reviewed head 13707337; R4-1)

Lead update (D163 unconditional): A1 and A2 are ordinary criteria, rerun at the final head and
reported; not routed to the owner.

Rule. Every W5 next carries `seal`, a required field. At each visit of a next (arrival, and every
re-visit while parked: each owner turn at the authority, each revisit after a relay's own sync),
the upstream answers 200 when it has frames, else 204 at once when its seal S is above the
request's `seal`, else parks to its deadline (204). S is the authority's `sealed()->applied_index()`
(= the layouts table's `seal`, layout_export.cc:39) or a relay's mirror table seal. The upstream
keeps no seal state: `NamespaceSessions::Session::seal`, `restamp()` (both upstreams) and the
open's stamp go. The relay sends its mirror table's seal from its last good sync (s_r) on every
watch next. Its reading of answers stays: 200 or a 204 before the park, counted from the send, is
a change (sync); a 204 at or past the park is idle (wait again, same heads, same s_r). The watch's
open no longer reports a change (the open carries no stamp), so it costs no sync.

Why it fixes R4-1: a seal's 204 misread as idle no longer spends anything. The re-wait carries the
same s_r < S, so it answers 204 from its arrival, one round trip, which is under the park.

Lifecycle table (relay B reading upstream U; B's last sync read s_r = s; the seal moves U's S from
s to s+1). Each row is checked by hand against the rule:

| # | B's state when U applies the seal | U answers | B reads | B serves the seal | Test (one value check per row) |
|---|---|---|---|---|---|
| 1 | Idle, before its next reaches U (in the pacing gap, in flight, setup, queue) | at arrival S > s: 204 at once | 204 before the park: change | after RTT + sync | hook "between B's sync and its next" (both upstream kinds); seal_stamp `older` (stamp s-1 → 204 at once) |
| 2 | Waiting, parked at U; 204 reaches B before the park | revisit: S > s: 204 at once | change | at once + sync | hook "while B's next was parked"; seal_stamp `parked` |
| 3 | Waiting; U's 204 reaches B past the park (U started the park late: setup, TLS, U's queue, a relay U's sync ahead; R4-1) | 204 as in 2 | idle: re-wait at the same heads with s | re-wait: at arrival S > s, 204 at once, read early: sync. One extra RTT (no pacing delay: the previous ask was a park ago) | new hook `late_park` (R4-1's seal_late_park: U's mirror held 800 ms in a sync past B's park); seal_window `other` against a real authority |
| 4 | Answered late: U already answered (idle 204 or a 200) before the seal; the answer is on its way | the answer has no seal | idle: re-wait with s; 200: sync | idle: re-wait answers 204 at once (as 1). 200: the sync reads s+1, or s and then 1 | new hook "after an idle answer, before the re-wait"; seal_stamp `stale` (two nexts with stamp s-1 in one session both answer 204 at once: no state) |
| 5 | Syncing, before its layouts read | nothing yet | the sync reads s+1: s_r = s+1 | by that sync; its next parks (no spurious wake) | seal_stamp `current` (stamp = S parks the full wait) |
| 6 | Syncing, after its layouts read (s_r = s) | its next arrives with s < S: 204 at once | change | as 1 | as 1 (CI's case) |
| 7 | Opening or reopening the watch session (start, after 410, after a move) | the open answers 200; the next carries s_r | as 1 to 3 | as 1 to 3; the open itself causes no sync | test_http_upstream `idle`: 0 syncs at rest from the first sync (was 1 for the open); hook "before B's watch session opened" |
| 8 | Its next answered 409 or 410 | 409: B retries; 410: B reopens (7) | as before | as 7 | seal_stamp `busy` ([204 or park, 409]), `ended` (410, new session's next with stamp = S parks) |
| 9 | Deadline 204, no seal | 204 at the park | idle, never early | n/a: past maximum_wait_ms since its last sync B syncs | test_http_upstream deadline sync at 4 s |
| 10 | B's sync reads a source behind the watch's (after a rotation: s_r < S at the watch) | 204 at once on every next | change each time | syncs at most one an interval (pacing) until its reading source catches up; polling's rate, bounded | none new: rotation steps in test_http_upstream `failed_wait` |

Degenerate case, stated in the doc: when U's round trip reaches the park (setup at least half the
timeout), row 3's re-wait is misread too and B serves the seal at maximum_wait_ms. That is a
source that hangs by F1a's bound.

Tests at both upstream kinds (authority and relay A), one value check per row; fails-without by
one-line mutants at the fix (refs/mutants/), and R4-1's two probes with `--fails-without` at
13707337 (seal_late_park and seal_window need no wire field, so they run there).

### Round 3: the seal stamp (code b76ae636, main a0d804bd merged at 13707337)

msg-1 (lead: option (a) approved, set widened by namespace_sessions.{hh,cc}, V5): done as below.
The wait mechanism changed, so it goes back to the correctness reviewer (whole mechanism, max).
- Mechanism: each W5 session holds a seal stamp, the upstream's seal at its last answer or open:
  `NamespaceSessions::Session::seal` and `restamp()` (authority, seal_at per owner turn), and the
  relay's `Session::seal` and `restamp()` (mirror table seal). Each visit of a next restamps; a
  next whose stamp was older answers 204 at once, parked or new. `seal_seen`, `was_parked` and the
  relay's `resealed` are gone (grep finds none). `HttpUpstream::wait` reports a change when it
  opens or reopens its watch session (`next(..., watch)`). MirrorStatus gains `seal` and `parked`
  (test observation). One doc: docs/mechanisms/relay-mirror-wait.md, "The seal stamp" (table);
  relay-mirror-seal-stamp.md is deleted.
- Class: every transition of the stamp table, at both upstreams. One value test each, with its
  fails-without (mutants under refs/mutants/, one replacement each,
  .scratch/relay-mirror-idle-wake/mutants/make.py):
  | Transition | Test | Fails without |
  |---|---|---|
  | open stamps | seal_stamp `open` (parks 1.5 s), both | stamp-open-a, stamp-open-r |
  | next while parked | seal_stamp `busy` ([204, 409]), both | stamp-busy-a, stamp-busy-r |
  | next, stamp older (CI's case) | seal_stamp `older` (204 in < 0.8 s), both; http_upstream hook "between B's sync and its next" | stamp-older-a, stamp-older-r |
  | seal applied while parked | seal_stamp `parked`, both; http_upstream "while B's next was parked" (waits for A's parked == 1) | stamp-parked-a, stamp-parked-r |
  | answer restamps | seal_stamp `answered` (the next after the early 204 parks), both | stamp-answer-a, stamp-answer-r |
  | session end | seal_stamp `ended` (410, a new session stamped at its open), both | stamp-close-a, stamp-close-r |
  | watch opens its session (reader) | http_upstream hook "before B's watch session opened" | stamp-open-u |
  CI's case is deterministic: `Hooked` runs a hook in B's watcher before the wait, which reseals A
  and returns once A serves the seal (`reseal_adopted`, A's status().seal). The Python test orders
  each seal by observing the upstream's layouts seal, and its parked step relies on seal_after (1 s)
  between the write and the seal.
- Local at b76ae636: TSan test_http_upstream (.scratch/build-tsan, CI's flags) 5 of 5 passed, no
  TSan report: seal before open 73-98 ms, while parked 160-182 ms, between sync and next 107-134 ms.
  Release (`quiet light`): test_http_upstream, test_relay_seal_stamp (authority older 7 ms, relay
  16 ms; parked 2.35/2.51 s; the rest parked 1.5 s), test_relay_idle, test_relay_service,
  test_relay_hung, test_relay_cut-off, test_relay_chain, test_relay_mirror, test_client_reset,
  test_authority_frames_service, test_authority_idle_wake, test_authority_multivoter,
  test_delivery_namespace_session, test_namespace_coordinator passed.
- Tests that counted syncs at rest now wait for the sync after the watch session opens
  (test_http_upstream `idle` and `paced`); `idle` at rest is now exactly 0 syncs, not at most 1.
- readiness_pressure (CI TSan, run 45542785995): unexplained, and not this quest's. It timed out in
  `child.wait(timeout=5)` after `child.kill()`. main's TSan passed it in runs 45482941635,
  45451330785 and 45352234452.
Queued once at 13707337 (jobs.txt in .scratch/relay-mirror-idle-wake):
- test_http_upstream 261124-233712-62aab8; fails-without stamp-older-r 261124-233717-f5e430,
  stamp-parked-r 261124-233721-7bae55, stamp-open-u 261124-233725-c02fa6, paced-off
  261124-233730-a95aa5, skip-off 261124-233734-b457de.
- test_relay_seal_stamp 261124-233739-86bdee; fails-without stamp-open-a 261124-233743-924fd7,
  stamp-busy-a 261124-233748-de8d7a, stamp-older-a 261124-233752-694f7a, stamp-parked-a
  261124-233757-fab66e, stamp-answer-a 261124-233802-d2e0b5, stamp-close-a 261124-233806-589a92,
  stamp-open-r 261124-233811-6aa80b, stamp-busy-r 261124-233815-d50bfc, stamp-older-r
  261124-233819-2443ee, stamp-parked-r 261124-233824-aa3d6e, stamp-answer-r 261124-233828-8e3455,
  stamp-close-r 261124-233833-e40242.
- test_client_reset 261124-233838-9a08ad; fails-without client-off 261124-233842-de1dba.
- test_relay_idle 261124-233847-d27d26; fails-without runtime-at-main (with the sessions at main)
  261124-233851-8c0429, relay-f1-9337 (with the sessions at main) 261124-233856-4d800e.
- test_relay_hung 261124-233901-c578fd, fails-without 261124-233905-9e80bf; test_relay_cut-off
  261124-233910-6acdab, fails-without 261124-233914-0d0617 (relay-f1-9337).
- A1 261124-233918-fc8eca, A2 261124-233923-494ac5 (250 ms) and 261124-233927-d29ae2 (10 ms),
  all exclusive against main a0d804bd. Suite 261124-234020-0cb2a9 (at 5bde2b99, same code).
  CI on the pushed branch.

### CI round (run 45542785995 at 6f58aa77, code 75021884)

1. test_http_upstream:761 (linux-tsan, linux-asan-ubsan): the seal is missed, not slow, and pacing
   plays no part. Timeline (CI log): change reached B in 210 ms (tsan) and 57 ms (asan); the
   deadline sync came after 4113/4108 ms; the seal reached B after 4115/4106 ms, which is B's next
   4 s deadline, not a slowed early 204. The step reseals A right after B's deadline sync. B's next
   reached A only after A (polling its fake voter every 10 ms) had adopted the seal. An upstream
   answers early 204 only to nexts parked when it applies the seal, so B's next parked at unchanged
   heads until the deadline. This is review R2-4 (accepted as a gap); a sanitizer's slower round
   trip widens the window until the test always lands in it. Pacing: interval 10 ms, and the
   previous ask was about 1 s earlier (one 1 s park), so the gate waits 0 ms. Scaling the bound
   would hide a real miss, so it is not done. The fix (a per-session seal stamp) changes the wait
   mechanism and needed src/detent_authority/namespace_sessions.{hh,cc}; the lead approved it (round 3).
   Local TSan build (.scratch/build-tsan, CI's flags, `quiet light`), 3 runs at 75021884: the seal
   reached B in 169 ms, 155 ms and 4264 ms (the third failed :761). The result is bimodal, prompt or
   at the deadline, never in between, so this is a miss, not a slowdown: a slower build would shift
   every run.
2. test_authority_readiness_pressure (tsan): the timeout is `child.wait(timeout=5)` right after
   `child.kill()` (test_multivoter.py:486-487). The process had been SIGKILLed, so no Detent code
   ran during the 5 s; what took the time was the kernel tearing the process down (TSan's shadow
   mappings, plus io_uring cancellation: the health shows io_native). main's TSan passed it in runs
   45482941635, 45451330785 and 45352234452, and no other branch run since then (45553576282,
   45533937366, 45513889725, 45470398185, 45462131792) failed it, so I can't show it as
   pre-existing. The lead ruled it unexplained and not this quest's.
3. A2 14371c and 702b6d failed at head-1: "the parked next answered 204, not the change". Since N2,
   every seal answers parked nexts 204 (R2-5), and the probe treated a 204 as a failure. The probe
   now parks again after a 204, as a W5 reader does (bench/idle-wakeups/relay_latency.py).
   Local check (Release, 8 samples at 250 ms, `quiet light`, not evidence): p50 26.7 ms, p99 165.6.
   A2 is queued at round 3's head.
A1 2bfd93 (75021884 vs a69f43a4, 3 x 20 s, exclusive): relay 0.076-0.077% of a core against main
1.047-1.086%, 0 syncs/s against 3.85-3.9; authority 0.121-0.127% against 1.487-1.53%, 4.8 owner
turns/s against 20.1-20.4. Net -2.39% of a core per relay and authority pair = 2,065 core-s a day
(about 0.24 W at 10 W a core). Suite 5f884c passed at 75021884.

### Round 2 notes (75021884)

Round 2 review APPROVE at 29c32169 (reviews/relay-mirror-idle-wake-correctness.md, 26f645a5); its
notes R2-1..R2-3 fixed at 75021884 (main 2c825a49 merged: campaign files only).
- R2-1: the watcher asks no sooner than `interval` after its previous wait (mirror_service.cc
  watch_loop, `asked`). Class: every path that re-arms a wait (idle re-wait, change, failure); one
  gate before each ask covers all. Test test_http_upstream `paced`: timeout 400 ms (200 ms parks),
  interval 1 s: A answered 3 idle waits in 3 s, B 0 syncs (local); fails-without paced-off.
- R2-2: client.cc maps 503 not_ready to Unavailable (rotates, coordinator.cc unavailable_reset).
  Test tests/detent_delivery/test_client_reset.cc (value per 503/429 error), registered in
  tests/detent_relay/CMakeLists.txt (tests/detent_delivery/CMakeLists.txt is outside the set);
  fails-without client-off.
- R2-3: test_http_upstream `failed_wait`: an upstream whose first wait throws; 1 skip, and the sync
  after it saw it, 2 syncs; fails-without skip-off.
Local `quiet light` at 75021884: test_client_reset, test_http_upstream, test_relay_idle (seal after
2.80 s), test_relay_hung (0.46 s), test_namespace_coordinator passed.
Queued once at 75021884 (mutants under refs/mutants/; hunk reverts of the head file):
- test_http_upstream 261124-194930-93c649; fails-without relay-at-main 261124-194933-393865,
  paced-off 261124-194938-f72663, skip-off 261124-194942-93b926, relay-f1-9337 (fix files at
  9337f6c9, 29c32169 tests: reseal) 261124-194946-4672e0.
- test_client_reset 261124-194951-3fd12d; fails-without client-off 261124-194955-c8f24b.
- test_relay_idle 261124-194958-4ef9df; fails-without runtime-at-main 261124-195002-0ce6a0,
  relay-f1-9337 261124-195005-70f43b.
- test_relay_hung 261124-195008-115e1d; fails-without 261124-195012-4047b1 (relay-f1-9337).
- test_relay_cut-off 261124-195015-196331; fails-without 261124-195018-215053 (relay-f1-9337).
- A1 261124-195021-2bfd93 (75021884 vs a69f43a4); band 261124-191512-3908aa (main vs main, kept:
  independent of the fix). A2 261124-195024-14371c (250 ms), 261124-195027-702b6d (10 ms).
- Suite 261124-195030-5f884c. CI on the pushed branch.
Cancelled as superseded (29c32169): 2c98e3 bcf6ff c5282a 92866f ca4837 377db8 7888d0 265462
a5cd86 9ee26e af2c4a 0afbd5 97c202 b185e1.

### Round 1 (29c32169)

Correctness review CHANGES at 9337f6c9 (.campaign/detent/reviews/relay-mirror-idle-wake-correctness.md),
fixed at 29c32169 (main a69f43a4 merged; no source change since e9149575).
- F1 (blocking), both lead-ruled parts. Class: a source that stops answering while it holds the
  parked wait; its transitions are the voter-state table in docs/mechanisms/relay-mirror-wait.md
  (serving idle, serving publish, hung, cut off, restarting or gone, leader change).
  (a) HttpUpstream parks at most timeout/2 and the request's deadline is the timeout (no `longer`);
  a failed wait makes the mirror `skip()` past that source before its sync; an idle 204 re-parks at
  the same heads until maximum_wait_ms after the last good sync (deadline sync kept).
  (b) service_runtime.cc: a W5 next, parked or new, answers 503 not_ready while `!serving()`.
  Tests: test_relay_hung.py `hung` (reviewer's hung_follower.py made a test; SIGSTOP the voter the
  relay's wait holds, stale <= 2.5 s; local 0.46-0.56 s) and `cut-off` (pause both peers of a
  follower holding a parked next: 503 not_ready, local after 1.93 s, bound 25 s). Restarting/gone
  is the existing A stop/return step in test_http_upstream idle; serving idle and publish are its
  idle and change steps.
- N2 seal: main adopted it within one interval, so it was a regression; fixed. The authority answers
  nexts parked across a new seal 204 at once (`seal_seen`/`resealed`), a relay does the same once a
  sync adopts a newer table seal, and the relay takes a 204 before its wait as a change (a deadline
  204 can't come sooner than the wait, timed from the send). Tests: test_http_upstream idle reseal
  step (local 218-308 ms, bound 1.5 s, deadline 4 s); test_relay_idle seal step (sealed file
  fetched, local 2.49 s, bound 5 s, deadline 30 s).
- N1: A2 rerun at the final head (below). N3: docstring says 250 ms health turns, 4 a second.
  N4: no change (reviewer: none needed).
Local `quiet light` at 29c32169: test_http_upstream, test_relay_idle, test_relay_hung,
test_relay_cut-off, test_relay_service, test_authority_frames_service, test_authority_idle_wake,
test_authority_multivoter, test_authority_lost_quorum, test_delivery_namespace_session passed.
Queued once at 29c32169 (mutants under refs/mutants/):
- test_http_upstream 261124-191358-2c98e3; fails-without 261124-191402-bcf6ff (relay-at-main
  caf12c70) and 261124-191407-c5282a (relay-f1-9337 e0648da3: the five fix files at 9337f6c9).
- test_relay_idle 261124-191411-92866f; fails-without 261124-191415-ca4837 (runtime-at-main
  a1787553) and 261124-191419-377db8 (relay-f1-9337).
- test_relay_hung 261124-191423-7888d0; fails-without 261124-191427-265462 (relay-f1-9337).
- test_relay_cut-off 261124-191432-a5cd86; fails-without 261124-191436-9ee26e (relay-f1-9337).
- A1 exclusive 261124-191537-af2c4a (vs main a69f43a4); band 261124-191512-3908aa (band-at-main
  29b28e1c: all six fix files at main). A2 exclusive 261124-191516-0afbd5 (250 ms),
  261124-191520-97c202 (10 ms). Suite 261124-191550-b185e1. CI on the pushed branch.
Cancelled as superseded (9337f6c9): 7f5598, 61b78c, fef9a7, 3c4319, d0505f, 1a1358, 9c056a, 2b809c.
Suite eb494f at 9337f6c9 had finished (reviewer: 219/219).

## Design (sent to the lead as msg-310)

No new endpoint or parameter: W5 next's existing `wait_ms` parks at the authority, which re-plans
at every applied entry (service.cc ~2851) and answers on change, 204 at its deadline. An absent
name gets no frame until created (frames_export.cc), so a cursor-less position wakes on creation.
- `Upstream::wait(positions, limit, stop_token) -> bool`; default returns true at once (a poller:
  in-process fakes keep syncing every interval).
- `HttpUpstream::wait`: a parked next on its own W5 session (second channel), at the endpoint of
  the last sync; safe beside the mirror thread's calls; the stop_token closes its socket.
- MirrorService: a watcher thread waits on the ring heads after each good sync; a change, the
  deadline (maximum_wait_ms, clamped by the session's wait) or a failure wakes one sync, no
  sooner than `interval` after the last. Failed syncs keep the backoff and arm no watch.

## Next

1. Once the set is widened and the agents' stamp ruled: code the rule (table above), port R4-1's
   probes into tests, the class tests, mutants; then queue A1, A2, A3, A6 (with fails-without at
   13707337), the suite, and push.

## Blocked on

Lead: D163 puts a field on the next request; everything that parses, builds, copies or hand-writes
that body is outside the set. Needed:
- include/detent_distribution_http/session_request.hh, src/detent_distribution_http/session_request.cc
  (parse the required `seal` on next).
- include/detent_delivery/namespace_session.hh, src/detent_delivery/namespace_session.cc (the body
  builder and `next_namespace_session` take the stamp), src/detent_delivery/coordinator.cc:1054
  (the agent's next).
- src/detent_authority/service.hh (`Task::seal`), src/detent_authority/service.cc:560-571 (copy it).
- src/detent_relay/service.cc:386 (`job.seal = command.seal`).
- Tests that write a next body: tests/detent_delivery/test_namespace_session.cc,
  test_session_request.cc, namespace_server_fixture.hh; tests/detent_authority/test_frames_service.cc,
  test_distribution_ownership.cc; tests/detent_system/run_trust_negatives.py.
- The W5 spec: docs/status/namespace-s6.md (the request block), docs/SUBSCRIPTIONS_ARCHITECTURE.md:17.

Decision (lead's, user-visible: agents' wakes): what an agent, which never reads layouts and knows
no seal, sends as `seal`. Sending 0 would answer every agent next 204 at once, a spin.
1. Recommended: the field means "answer at once once your seal is above this"; agents send the
   largest value (2^64-1), so a seal never wakes them, as on main. No answer changes, no agent
   state, the cheapest; the cost is one documented value with a meaning of its own.
2. Every next answer (200 and 204) carries the upstream's seal in a header, and agents echo the
   last one. Agents then wake once per seal while parked (round 3's cost, review note 1), and both
   upstreams' replies gain headers (service.hh `Reply` and the relay's `control` have none), so the
   set widens further. A relay could then also drop the timing test, but it no longer needs to.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), name every job you examined, failures included (`campaign brief` lists failed
jobs no journal names), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | 261124-233918-fc8eca (13707337 vs main a0d804bd), queued; at 75021884 2bfd93 (Doing) |
| A2 | 261124-233923-494ac5 (250 ms), 261124-233927-d29ae2 (10 ms), queued; 14371c and 702b6d failed on the probe (fixed) |
| A3 | test_http_upstream 62aab8 (fw f5e430, 7bae55, c02fa6, a95aa5, b457de); test_relay_seal_stamp 86bdee (fw 924fd7, de8d7a, 694f7a, fab66e, d2e0b5, 589a92, 6aa80b, d50bfc, 2443ee, aa3d6e, 8e3455, e40242); test_client_reset 9a08ad (fw de1dba); test_relay_idle d27d26 (fw 8c0429, 4d800e); test_relay_hung c578fd (fw 9e80bf); test_relay_cut-off 6acdab (fw 0d0617); queued; first fails-without 261124-033951-a94472 and 261124-191402-bcf6ff |
| A4 | [artifact] docs/mechanisms/relay-mirror-wait.md (voter-state table, the seal stamp table) |
| A5 | suite 261124-234020-0cb2a9, queued; CI runs 45569989196 and 45569989739 at 410fb793 (pushed); at 75021884 5f884c passed |

Jobs examined: 261124-033656-d26b03 (smoke), 261124-033951-a94472 (fails-without, older test,
exit 8 as required), 261124-034543-d72b56 (probe, cancelled), 261124-034500-c0335d (A/A, superseded
and cancelled), 261124-035147-903054 and 261124-035149-ce2080 (failed: test bugs above; suite's
test_agent_store_crash timeout is pre-existing). msg-310: answered by the lead (set widened). msg-1:
answered in Doing (round 3). Superseded at 75021884, not rerun: 93c649, 3fd12d, 4ef9df, 115e1d,
196331 and their fails-without (round 3 reruns each at 13707337).

## Result

The mirror waits on its upstream with a parked W5 next on a second session and syncs on its
return (change, deadline, failure); docs/mechanisms/relay-mirror-wait.md, CHANGELOG.md.

A1, 981777 (fix 80393d76 vs main ba79caae, 250 ms, 3 x 20 s interleaved, exclusive); band 92ed4b
(main twice): relay 1.12-1.19% of a core, authority 1.59-1.68%.
| | main | fix |
|---|---|---|
| relay CPU, % of a core | 1.117, 1.098, 1.095 | 0.059, 0.060, 0.057 |
| relay syncs/s | 3.80-3.85 | 0.0 |
| relay interrupt wakeups/s | 36.2-36.6 | 11.1 |
| authority CPU, % of a core | 1.552, 1.536, 1.521 | 5.159, 5.395, 5.013 |
| authority owner turns/s | 19.9-20.1 | 773-788 |
| authority interrupt wakeups/s | 68-69 | 856-862 |
Relay: -1.044% of a core = 902 core-s a day per relay (about 0.1 W at 10 W a core). The relay's
residual 0.059% is its host turn (9.9 turns/s, unchanged), no mirror work.
Authority: +3.65% of a core = 3,156 core-s a day (+0.37 W): a parked W5 next makes the authority's
owner turn every ~1 ms (service.cc:3337, `!frames_tasks.empty()` counts as transient work). That
cost is per authority while any next is parked (agents park nexts there today), not per relay.
Net for one relay on one authority at 80393d76: +2.6% of a core. Fixed at dec62ada (now
service_runtime.cc): locally the authority idles at 4.0 owner turns/s with the relay parked (main
~20, the spin ~780); A1 d0505f gives the net.

A2 (p50 / p99 ms, 3 rounds each): at 250 ms, 3ac6f3: main 158.8/329.9, 166.7/325.3, 157.3/269.6;
fix 31.0/169.5, 31.1/171.9, 31.7/182.1 (A/A band 387342: p50 168-186, p99 309-345). At 10 ms,
314fca: main 33.7/180.0, 37.4/165.1, 34.4/162.5; fix 31.0/190.7, 31.1/172.4, 31.1/181.0: p50 3-6 ms
lower, p99 +7 to +16 ms, inside the 250 ms A/A p99 spread (36 ms) but with no 10 ms A/A row.

## Rulings I made

- `interval_ms` stays and becomes the least time between syncs (and the backoff's base): a stream
  of changes costs at most one sync an interval, as polling did, so no config change.
- The wait's deadline is the relay's `maximum_wait_ms` (30 s), bounded by the upstream session's
  `wait_ms`; no new option. Each parked request is bounded by half `timeout_ms` (F1a): an idle relay
  makes one parked request per 5 s at defaults and one sync per 30 s.
- F1a's bound is half the timeout, not the timeout: the request deadline is the timeout, so a
  healthy idle answer always lands before it, and a silent source is one that hangs.
- A wait that fails makes the mirror `skip()` past its source before syncing, so the sync reads the
  next source at once instead of meeting the hung one and timing out again.
- R2-1's gate paces every wait, not only idle re-waits: a change's wait already starts long after
  the previous ask, so only back-to-back asks are delayed. No configuration bound on timeout_ms.
- test_client_reset's source sits in tests/detent_delivery (the widened set) and is registered from
  tests/detent_relay/CMakeLists.txt, which is in the set; the lead may move the registration.
- Seal signal (N2, round 3): a 204 before the wait, not a new field or status, so no protocol
  change. Which nexts get it is the session's seal stamp (lead-approved design), not `was_parked`.
- The authority's stamp lives in the session record and is set by `restamp()` at each visit of a
  next (a stale one always answers in that visit), not carried in `Task` (service.hh is outside
  the set). A stamp older than the truth costs one extra 204; a newer one would miss a seal, so
  every stamp is taken at or before its answer.
- MirrorStatus gains `seal` and `parked` so the tests observe A's state instead of timing it.
- test_relay_seal_stamp.py sits in tests/detent_relay (in the set) though half of it checks the
  authority: one script runs the same table against both upstreams.
- The watch is a signal: its answer's frames are dropped and the sync re-reads them (one extra
  request per change; NamespaceRelay unchanged). Under constant churn that is 3 requests per
  interval instead of 2.
- Names the upstream lacks are watched with no cursor (the authority answers on creation). A name
  the upstream refuses (403) makes the wait fail at once: that relay polls every interval, as before.
- The watch's session follows the endpoint of the last sync and never rotates the reading one.

## Notes for relay-service-refused-sync-flake

test_relay_service.py's D9 step relied on C polling its lagging proxy; it now writes once to wake C
(this quest's edit). The proxy's 10 s upstream timeout would cut a 30 s parked next; raised to 45 s.

<Decisions inside the quest's scope, so the reviewer can check them.>
