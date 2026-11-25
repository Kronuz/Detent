# relay-mirror-idle-wake: journal

State: ready for review (round 5 at Detent:e200ee50; jobs queued)
Quest: .campaign/detent/quests/relay-mirror-idle-wake.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

### Round 5: D165, the seal on the answer (review round 5 CHANGES at 5f601d6b: R5-1..R5-3)

Lead: fix R5-1..R5-3; D165: every answer to a next, 204 or 200, carries the upstream's seal; the
relay compares it with its stamp and reads nothing from timing; agents ignore it (D164 stands).
Done in the worktree, uncommitted parts committed as WIP below; it does not build until the
blocker is widened (two call sites, below).
- Wire (D165): header `X-Detent-Seal` (`distribution_http::seal_header`, session_request.hh). The
  authority's 204 (`idle()` in the frames pass, seal_at) and 200 (export metadata, the sealed
  index at start) carry it; `Service::Reply` gains `headers`, which stream_task passes to
  send_control. The relay's 204 (`idle(job)`) and 200 (`answer`'s headers) carry its mirror seal;
  relay service.cc's `control` passes `reply.headers`. `HttpUpstream::wait`: 200 is a change; a
  204 is a change when its seal is above the stamp; a 204 with no or a bad seal refuses the watch
  and throws. `Response::early` and the send-time test are gone.
- R5-3, wire-older-r: the relay's handler no longer checks the stamp; `revisit`, which runs at the
  end of every turn after `handle`, is the one place a stamp is answered (as the authority has one
  expression per visit). A new next with a stale stamp waits at most for a sync running in its
  turn.
- R5-3, b0740b: test_http_upstream `idle` parks 2 s (timeout 4000), longer than each step's 1.5 s
  bound, so wire-parked-r's fallback to A's deadline fails the step by 0.5 s, not 40 ms.
- R5-3, ac469a: dropped with wire-older-a. The authority computes `sealed` in one expression for
  every visit; the mutant only delays a new next by one owner pass, which no bound separates
  safely from a sanitizer's pass.
- R5-1: compare.sh builds test_relay_probe at the baseline too; each arm runs its own build's
  probe. Main's probe has no `--heads`, so the build copies this checkout's relay_probe.cc into
  the baseline checkout for its build and restores it on exit (a job's ref checkout is reset per
  job): the probe speaks main's W5 through main's HttpUpstream (checked: head's relay_probe.cc
  compiles -fsyntax-only against main's headers). `--agent-seal` (seal 2^64-1 in relay_latency.py and paths.py) goes only to a build
  whose session_request.cc takes `"seal"` (main: 0 matches; the fix: 2).
- R5-2 test: test_relay_seal_shortwait (ctest) runs bench/idle-wakeups/seal_window.py with the
  authority's `subscription_limits.wait_ms` 0, 6 trials, 1.5 s bound (the reviewer's
  seal_shortwait, folded into seal_window as AUTH_WAIT_MS). test_http_upstream `no_park`: relay A
  grants no park (maximum_wait_ms 0): B syncs 0 times in 1 s at rest, and A's seal reaches B within
  1.5 s.
- test_relay_seal_stamp checks the answer's seal at every step (Pair.exchange returns headers).
- Docs: relay-mirror-wait.md (the table below, the wake table), namespace-s6.md, CHANGELOG.

State table, extended with the answer's seal and a park of 0 (B's stamp s; U's seal s -> s+1):

| # | B when U applies the seal | U answers (status, seal) | B | Test |
|---|---|---|---|---|
| 1 | Between its sync's layouts read and its next reaching U | 204 at once, s+1 | change: syncs | http "between"; stamp `older`, `stale` (seal = S) |
| 2 | Parked at U | 204 at once, s+1 | syncs | http "while parked" (2 s park); stamp `parked` (seal in s+1..after) |
| 3 | Parked; U's 204 reaches B past the park (R4-1) | 204, s+1 | syncs: the seal decides, not the time | http `late_park`; seal_window `other` |
| 4 | U grants no park (session `wait_ms` 0; R5-2) | every next 204 at once, its seal | syncs on the first answer above s; at rest waits again once an interval | http `no_park` (relay U), test_relay_seal_shortwait (authority U) |
| 5 | Answered already (idle with s, or a 200) | nothing new | idle: waits again with s, answered 204 s+1 at once: syncs; 200: syncs | http "after an idle answer" |
| 6 | Syncing, before its layouts read | nothing | reads s+1; its next parks | stamp `current` (204, seal = S, parks) |
| 7 | Opening its watch session | the open carries no stamp | as rows 1-5; no sync for the open | http `paced` (1 sync in all) |
| 8 | 409, 410 | no seal (errors) | retry; reopen (7) | stamp `busy` (the parked 204's seal = S), `ended` |
| 9 | Reading a source behind the watch's | every 204 above s | syncs at most once an interval | (rotation: `failed_wait`) |
| 10 | Idle at the deadline, no seal | 204 at the park, s | waits again; at maximum_wait_ms syncs | http deadline sync at 4 s |
| 11 | A 204 with no seal (a broken source) | 204 without the header | refuses the watch session, `Failed`: skip and sync | (by reading; one shape: no fallback) |
| D164 | Agent (stamp 2^64-1) | 204 at its deadline through a seal (seal carried, ignored); 200 on a change | as main | stamp `agent across a seal`, `agent and a change` (200's seal in before..after) |

Mutants for this round (make5.py, after the blocker): drop wire-older-r and wire-older-a; rewrite
wire-open-u as an open that returns a 200 (`Response` lost `early`); add wire-header-a (the
authority's 204 seal "0"), wire-header-r (the relay's), and wire-compare-u (`wait` reads only 200
as a change). Pairs: stamp test for -a/-r header mutants, shortwait for wire-header-a, http for
wire-compare-u and wire-header-r; shortwait's own fails-without at 5f601d6b via `--ref`. Every
pair run locally on a mutant tree before it is queued.

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

msg (lead, unblocked): set widened as listed; D164 takes option 1 (agents send 2^64-1). Done at
code 5f601d6b (main 1daf9756 merged at 516d3460, campaign files only):
- Wire: `seal` required on a v2 next (session_request.cc; refused when absent, negative or not a
  number). `Following::seal` (default 2^64-1, the agent's value; coordinator.cc unchanged) goes
  into every next body. `Task::seal` and `MirrorJob::seal` carry it; the authority answers
  `seal_at > task.seal`, the relay `seal() > job.seal`, at each visit, parked or new.
- Removed: `NamespaceSessions` back to main (no stamp, no `restamp`); the relay's `Session::seal`
  and `restamp`; `HttpUpstream::next`'s `watch` early return. `Upstream::wait` takes the stamp; the
  watcher sends the seal of its last good sync (`Watch{positions, seal}`).
- Docs: relay-mirror-wait.md "The seal stamp" (the table above, as a doc), namespace-s6.md (W5
  request and 204), CHANGELOG. SUBSCRIPTIONS_ARCHITECTURE.md describes version 1, which has no
  seal: unchanged.
- Tests per row (Release, local `quiet light`, 3 runs of test_http_upstream):
  | Row | Test, local value | Fails without (refs/mutants/, make4.py) |
  |---|---|---|
  | 1, 6 | http "between B's sync and its next" 106-119 ms; stamp `older` 6 ms (authority), 20 ms (relay) | wire-older-r, wire-send-u, wire-copy-r; wire-older-a, wire-copy-a |
  | 2 | http "while B's next was parked" 155-165 ms; stamp `parked` 2.34/2.38 s (seal_after 1 s after the write) | wire-parked-r; wire-parked-a |
  | 3 (R4-1) | http `late_park`: A adopted the seal at +1360 ms from B's send, B served it 114 ms later (bound 1.5 s; 10 s deadline); seal_window job | wire-older-r; R4-1 probes at 13707337 |
  | 4 | http "after an idle answer, before B's wait again" 99-105 ms; stamp `stale` 6/20 ms (same stamp twice) | wire-older-r; wire-state-r (`stale`) |
  | 5 | stamp `current` parks 1.51/1.53 s; http `idle` 0 syncs in 1.5 s | wire-current-a, wire-current-r, wire-zero-m |
  | 7 | http `paced`: 1 sync in all (the open costs none); hook "before B's watch session opened" 95-101 ms | wire-open-u |
  | 8 | stamp `busy` [204, 409], `ended` 410 then a new session parks 1.51/1.53 s | stamp-busy-a/-r, stamp-close-a/-r |
  | 9 | http deadline sync at 4.1 s | (unchanged) |
  | D164 | stamp `agent`: 2^64-1 parks 4.0 s through a seal, a write to a answers 200 in 0.39-0.57 s | wire-agent-a, wire-agent-r |
  | wire | parser: no seal / -1 / "1" refused, 7 and 2^64-1 read; body ends `"seal":18446744073709551615` | wire-optional, wire-body |
  Mutants all pass `-fsyntax-only` with the build's flags (mutants/syntax.py), so a fails-without
  fails on the test, not the build.
- Local `quiet light` at 5f601d6b, all passed: test_http_upstream (x3), test_relay_seal_stamp,
  test_relay_idle, test_relay_service, test_relay_hung, test_relay_cut-off, test_relay_pages,
  test_client_reset, test_authority_frames_service, test_distribution_session_request,
  test_delivery_*, test_namespace_coordinator, test_agent_catch_up, test_pages_coordinator,
  test_authority_distribution_ownership, test_coordinator_teardown.
- R4-2: round 3's jobs are superseded (their code is gone); R4-3: the stamp table it names is
  replaced by the wire table, which has no close column.
- test_relay_seal_stamp first failed `relay current` (204 after 0.5-0.7 s): the authority sealed
  twice more after the relay matched it (debug .scratch/relay-mirror-idle-wake/dbg). The relay
  steps now start once the authority's seal held 2.5 s and the relay serves it.

Queued once at cb6ce9f2 (code 5f601d6b; .scratch/relay-mirror-idle-wake/submit4.sh, jobs4.txt):
- test_http_upstream 261125-034204-ec02e4; fails-without wire-older-r 261125-033905-9f2de8,
  wire-parked-r 261125-033909-b0740b, wire-send-u 261125-033913-7f33b7, wire-zero-m
  261125-033916-5d9190, wire-open-u 261125-033920-7ad458, paced-off 261125-033924-61e2f5, skip-off
  261125-033928-c63ca4.
- test_relay_seal_stamp 261125-034206-861c71; fails-without wire-older-a 261125-033933-ac469a,
  wire-parked-a 261125-033938-089cd9, wire-current-a 261125-033942-07d53b, wire-agent-a
  261125-033947-0fc023, wire-copy-a 261125-033951-08b4e5, wire-older-r 261125-033956-509945,
  wire-parked-r 261125-034000-c4f1aa, wire-current-r 261125-034005-23cdb1, wire-agent-r
  261125-034011-9f1416, wire-state-r 261125-034016-ed5d3e, wire-copy-r 261125-034021-a78366,
  stamp-busy-a 261125-034026-72fc8d, stamp-close-a 261125-034031-7e84fc, stamp-busy-r
  261125-034035-25cad1, stamp-close-r 261125-034041-3fe3b8.
- test_distribution_session_request 261125-034045-e43fcd, fails-without wire-optional
  261125-034049-894b17; test_delivery_namespace_session 261125-034052-da3edf, fails-without
  wire-body 261125-034056-7106ae; test_client_reset 261125-034100-0f3c46, fails-without client-off
  261125-034104-788931; test_relay_idle 261125-034107-15fd57, test_relay_hung 261125-034111-2412e0,
  test_relay_cut-off 261125-034114-2c7d46.
- A6 (R4-1): seal_late_park at 13707337 (reviewer's probe, must fail) 261125-034209-d0d4dc;
  seal_window at the fix 261125-034212-eaeb52 and at 13707337 (must fail) 261125-034215-62a666
  (600 ms setup with timeout 2000 over 16 trials, then loopback with timeout 200 over 40).
- A1 strict 261125-034218-badf25; A2 strict 261125-034220-aef247 (250 ms), 261125-034223-700753
  (10 ms); all against main 6e2669af (no code change since a0d804bd).
- A5: suite 261125-034343-bbe132 (campaign verify at f550d3a2, pushed); CI on the pushed branch.
- Not cancelled (the cancel was refused by my permissions; the lead's): round 3's 26 superseded
  jobs, 261124-233717-f5e430 through 261124-234020-0cb2a9 (Doing, round 3), fc8eca running.

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

1. Read round 5's jobs (Evidence); fill Result with A1 and A2 at e200ee50.

## Blocked on

Nothing. (Lead widened reply_pump.{hh,cc} for D165's 204 header: `send_control` takes headers.)

## Evidence

Round 5 local (Release, quiet light, at e200ee50): 14 tests pass: test_http_upstream (no park: A
answered 17 waits at once, B 0 syncs in 1 s, A's seal reached B in 96 ms; late park 102 ms),
test_relay_seal_stamp (every answer's seal = the upstream's: authority 15..21, relay 21..27),
test_relay_seal_shortwait (6 of 6 seals in 88-147 ms; 0 syncs at rest), test_relay_service,
idle, hung, frames_service, distribution_ownership, session_request, namespace_session,
client_reset. All 28 make5.py mutants compile (-fsyntax-only).

Round 5 jobs at e200ee50 (A3 unless noted; fw = --fails-without at the mutant):
- http 021cd6; fw: wire-parked-r 678476, wire-send-u 0d2924, wire-zero-m 81831f, wire-open-u f0b893, wire-compare-u ae0046, wire-header-r fad576, paced-off f02b41, skip-off 8308ae
- stamp 93206e; fw: wire-parked-a 400518, wire-current-a 8c34d0, wire-agent-a c50a01, wire-copy-a d4b574, wire-parked-r 415684, wire-current-r cfc692, wire-agent-r d7f19c, wire-state-r ad76a8, wire-copy-r 4dc5df, wire-header-a daf94a, wire-header200-a 24103d, wire-header-r 436921, wire-header200-r d628fd, wire-pass-r 357c4d, wire-pump dbab8d, stamp-busy-a 7b855a, stamp-close-a 405c3e, stamp-busy-r 1cd5ec, stamp-close-r a7ed6d
- shortwait 5fbb47; fw wire-header-a d223b9; fw at 5f601d6b (R5-2) d64cca
- parser 36dd84 (fw wire-optional 207b90); body dbffe9 (fw 291147); client 4b2f11 (fw 75a8a9); idle b8021e; hung 0d8681; cut-off 4b349e
- R4-1: late-park-fw at 13707337 d0d4dc; window 16f787; window-fw at 13707337 f656b3
- A1 strict 3344f2; A2 strict 250 ms d5d3f0, 10 ms 00c484 (BASE main e2e9ce35)
- A5 suite: campaign verify (reruns at ready for review)
Dropped (R5-3): 9f2de8 and 509945 (wire-older-r: the relay's handler no longer checks the stamp),
ac469a (wire-older-a). b0740b's class (wire-parked-r on http) is 678476 with a 2 s park.

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), name every job you examined, failures included (`campaign brief` lists failed
jobs no journal names), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | 261125-034218-badf25 (cb6ce9f2 vs main), queued; at 75021884 2bfd93 (Doing) |
| A2 | 261125-034220-aef247 (250 ms), 261125-034223-700753 (10 ms), queued |
| A3 | Round 4, queued at cb6ce9f2: test_http_upstream ec02e4 (fw 9f2de8, b0740b, 7f33b7, 5d9190, 7ad458, 61e2f5, c63ca4); test_relay_seal_stamp 861c71 (fw ac469a, 089cd9, 07d53b, 0fc023, 08b4e5, 509945, c4f1aa, 23cdb1, 9f1416, ed5d3e, a78366, 72fc8d, 7e84fc, 25cad1, 3fe3b8); parser e43fcd (fw 894b17); body da3edf (fw 7106ae); test_client_reset 0f3c46 (fw 788931); test_relay_idle 15fd57; test_relay_hung 2412e0; test_relay_cut-off 2c7d46; R4-1 d0d4dc, eaeb52, 62a666; first fails-without at main 261124-033951-a94472 and 261124-191402-bcf6ff |
| A4 | [artifact] docs/mechanisms/relay-mirror-wait.md (voter-state table, the seal stamp table) |
| A6 | [decision] D163 at 5f601d6b, D164 (agents' 2^64-1): the stamp is the request's `seal`, no session state (Doing, round 4); R4-1 probes: late_park in test_http_upstream 261125-034204-ec02e4, fails-without at 13707337 261125-034209-d0d4dc; seal_window 261125-034212-eaeb52, fails-without at 13707337 261125-034215-62a666; A1 badf25 and A2 aef247, 700753 at the final head, queued |
| A5 | suite 261125-034343-bbe132 at f550d3a2 (pushed), queued; round 3 0cb2a9 superseded; CI runs 45569989196 and 45569989739 at 410fb793 (pushed); at 75021884 5f884c passed |

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
- Seal signal: a 204 before the wait, read against the request's `seal` (D163, round 4); the 204
  itself is unchanged, so a relay still tells an early 204 from a deadline one by time. A misread
  late one costs one more wait, answered at once.
- `Following::seal` defaults to 2^64-1, the agent's value (D164), so the agent's coordinator needs
  no change; the relay's watch is the only caller that sets it.
- A v1 next carries no seal (its parser and SUBSCRIPTIONS_ARCHITECTURE.md are unchanged): the
  stamp is a W5 field.
- seal_window.py is committed under bench/idle-wakeups (the reviewer's probe, docstring updated);
  its fails-without job at 13707337 runs the fix's copy through `--ref`, so both run one script.
- R4-1's seal_late_park becomes test_http_upstream `late_park` (the hook on the wait after an idle
  answer); its fails-without at 13707337 is the reviewer's own probe, which builds against that
  head's `Upstream::wait`.
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
