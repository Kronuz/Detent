# relay-mirror-idle-wake: journal

State: ready for review
Quest: .campaign/detent/quests/relay-mirror-idle-wake.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

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

1. Read the 29c32169 jobs: A1 af2c4a and band 3908aa into net cores and watts (relay + authority),
   A2 0afbd5 and 97c202, the A3 rows and fails-without, suite b185e1, CI; fill Evidence and Result.

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), name every job you examined, failures included (`campaign brief` lists failed
jobs no journal names), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | 261124-191537-af2c4a (29c32169 vs main a69f43a4), band 261124-191512-3908aa; queued |
| A2 | 261124-191516-0afbd5 (250 ms), 261124-191520-97c202 (10 ms), queued; earlier 3ac6f3, 314fca, band 387342 (80393d76) |
| A3 | test_http_upstream 2c98e3 (fw bcf6ff, c5282a); test_relay_idle 92866f (fw ca4837, 377db8); test_relay_hung 7888d0 (fw 265462); test_relay_cut-off a5cd86 (fw 9ee26e); queued |
| A4 | [artifact] docs/mechanisms/relay-mirror-wait.md (voter-state table, seal answer) |
| A5 | 261124-191550-b185e1 queued; CI on 29c32169 |

Jobs examined: 261124-033656-d26b03 (smoke), 261124-033951-a94472 (fails-without, older test,
exit 8 as required), 261124-034543-d72b56 (probe, cancelled), 261124-034500-c0335d (A/A, superseded
and cancelled), 261124-035147-903054 and 261124-035149-ce2080 (failed: test bugs above; suite's
test_agent_store_crash timeout is pre-existing). msg-310: answered by the lead (set widened).

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
- Seal signal (N2): a 204 before the wait, not a new field or status, so no protocol change; the
  authority answers only nexts that were parked before the seal turn (`was_parked`).
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
