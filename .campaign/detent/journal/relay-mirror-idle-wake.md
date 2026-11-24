# relay-mirror-idle-wake: journal

State: blocked on split-detent: landing; then move the authority edit and queue evidence
Quest: .campaign/detent/quests/relay-mirror-idle-wake.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Round 3 (lead's ruling: the authority regression is this quest's). Done locally, not queued:
- dec62ada: service.cc's owner loop counts a W5 next looked at for the current applied index as
  parked (`frames_busy` false, its poll/deadline into `frames_until`), so the owner sleeps until its
  health turn, a deadline or a publish; unlooked nexts and nexts waiting for history keep 1 ms.
- fddecc70: tests/detent_relay/test_relay_idle.py (single voter + relay at 250 ms; over 2 s at rest
  authority turns <= 60/s and relay syncs <= 1; a publish wakes the relay within 2 s). At fddecc70
  (no authority fix) it should fail on turns (~780/s per A1 981777): the fails-without.
- test_relay_service: waits for the wake write's seal before stopping A and B (a local failure:
  "relay a fetched again after a restart", 2 files of the late seal).
Local `quiet light` at dec62ada: test_relay_idle passed ("authority 4.5 owner turns/s, relay 0
syncs in 2 s"; "the relay synced after 259 ms"), test_relay_service, test_http_upstream,
test_authority_frames_service, test_authority_idle_wake, test_delivery_namespace_session,
test_namespace_coordinator passed.

Earlier rounds: A3 903054 and suite ce2080 failed on test bugs (restarted A at an older seal; the
wake write before the busy probe), fixed in 0ef266b3. Jobs 261124-102825-ffc13c, -102832-d10575,
-102840-e72d93 ran at 0ef266b3, before the authority fix: superseded, not evidence.

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

1. When split-detent lands: merge main, move dec62ada's edit to the split file holding the owner
   loop (likely service_runtime.cc; the lead widens the set), run the targeted tests locally.
2. Queue once on that final code: A3 (test_http_upstream), A3 fails-without (revert mutant),
   test_relay_idle and its fails-without (a mutant with the authority edit reverted), the suite
   (A5), push for CI, and one exclusive A1 rerun (idle relay + authority, interleaved with main)
   with its A/A band. A2 needs no rerun unless the A1 row moves the wake path's numbers.
3. Result: net cores and watts across relay + authority; Evidence; ready for review.

## Blocked on

split-detent landing (the lead's sequencing): it moves service.cc's owner loop to another file.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), name every job you examined, failures included (`campaign brief` lists failed
jobs no journal names), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | 261124-035150-981777 (fix vs main), band 261124-035211-92ed4b; at 80393d76, src as at head |
| A2 | 261124-035152-3ac6f3 (250 ms), 261124-035154-314fca (10 ms), band 261124-034503-387342; at 80393d76 |
| A3 | 261124-102825-ffc13c queued; fails-without 261124-102832-d10575 queued |
| A4 | [artifact] docs/mechanisms/relay-mirror-wait.md |
| A5 | 261124-102840-e72d93 queued; CI pending |

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
Net for one relay on one authority: +2.6% of a core until the authority waits properly.

A2 (p50 / p99 ms, 3 rounds each): at 250 ms, 3ac6f3: main 158.8/329.9, 166.7/325.3, 157.3/269.6;
fix 31.0/169.5, 31.1/171.9, 31.7/182.1 (A/A band 387342: p50 168-186, p99 309-345). At 10 ms,
314fca: main 33.7/180.0, 37.4/165.1, 34.4/162.5; fix 31.0/190.7, 31.1/172.4, 31.1/181.0: p50 3-6 ms
lower, p99 +7 to +16 ms, inside the 250 ms A/A p99 spread (36 ms) but with no 10 ms A/A row.

## Rulings I made

- `interval_ms` stays and becomes the least time between syncs (and the backoff's base): a stream
  of changes costs at most one sync an interval, as polling did, so no config change.
- The wait's deadline is the relay's `maximum_wait_ms` (30 s), bounded by the upstream session's
  `wait_ms`; no new option. A deadline return also syncs (seals that moved no head).
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
