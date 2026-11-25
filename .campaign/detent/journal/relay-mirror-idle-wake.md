# relay-mirror-idle-wake: journal

State: ready for review
Quest: .campaign/detent/quests/relay-mirror-idle-wake.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Final head 9337f6c9: main merged after split-detent (a341e8f1); dec62ada's authority edit moved
unchanged to src/detent_authority/service_runtime.cc (applied by `git apply --3way`). Local
`quiet light` at 9337f6c9: test_relay_idle ("authority 4.0 owner turns/s, relay 0 syncs in 2 s";
wake after 306 ms), test_http_upstream, test_relay_service, test_authority_frames_service,
test_authority_idle_wake, test_delivery_namespace_session, test_namespace_coordinator passed.
Queued once at 9337f6c9 (main e9149575: campaign commits only past the merge), branch pushed for CI:
- A3 test_http_upstream 261124-175307-7f5598; fails-without 261124-175312-61b78c at ccd850ec
  (head with the five relay-wait files at main).
- Loop turns test_relay_idle 261124-175317-fef9a7; fails-without 261124-175322-3c4319 at 35e38250
  (head with service_runtime.cc at main).
- A5 suite 261124-175328-eb494f; CI on the pushed branch.
- A1 exclusive 261124-175330-d0505f (9337f6c9 vs main, 3 x 20 s interleaved); A/A band
  261124-175351-1a1358 at 5da530c8 (head with all six fix files at main: main's daemons, this
  bench). 261124-175335-73e528 cancelled (ran main's older bench).
A2 is not rerun: the authority edit changes when the owner sleeps, not how a publish reaches a
parked next (local wake 306 ms; the A2 rows 3ac6f3 and 314fca stand, at 80393d76).
Pin-wave will touch mirror_service.cc:364 and service_runtime.cc:586 (one token each): expect a
one-line merge.

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

1. Read A1 d0505f and band 1a1358: net cores and watts across relay + authority into the Result;
   paste the Evidence table; answer review findings.

## Blocked on

Nothing.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), name every job you examined, failures included (`campaign brief` lists failed
jobs no journal names), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| A1 | 261124-175330-d0505f (9337f6c9 vs main), band 261124-175351-1a1358; queued |
| A2 | 261124-035152-3ac6f3 (250 ms), 261124-035154-314fca (10 ms), band 261124-034503-387342 (80393d76) |
| A3 | 261124-175307-7f5598, fails-without 261124-175312-61b78c; loop turns 261124-175317-fef9a7, fails-without 261124-175322-3c4319; queued |
| A4 | [artifact] docs/mechanisms/relay-mirror-wait.md |
| A5 | 261124-175328-eb494f queued; CI on 9337f6c9 |

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
