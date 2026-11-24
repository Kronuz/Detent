# relay-mirror-idle-wake: journal

State: waiting on jobs 261124-035147-903054 261124-035149-ce2080 261124-035150-981777 261124-035152-3ac6f3 261124-035154-314fca 261124-035211-92ed4b 261124-034503-387342
Quest: .campaign/detent/quests/relay-mirror-idle-wake.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Round 2 (lead's message after 80393d76). Causes, both in my test edits, not the fix:
- A3 903054 / suite test_http_upstream: restarted relay A is a fresh voter at seal 2 while B holds
  A's published seal 3, so every B sync is UpstreamBehind ("older seal") and the 30 s `until`
  times out. Fix: the restarted A publishes once (its seal is B's again); the timeout message now
  carries B's error. Polling at main would fail the same way; not a wake-path change.
- Suite test_relay_service: the write I added to wake C ran before the busy-next probe, which
  compares to `first`. Fix: the busy probe runs before the idle check and the write; the restart
  comparison uses `settled`, a converged probe taken just before A and B stop.
- Suite test_agent_store_crash (Timeout 900 s): pre-existing, not mine; the same timeout is in
  other quests' suites 261123-175720-145715, 261123-200442-1898b2, 261124-065709-635779 (agent
  store, no relay code).
Neither fix touches src, so A1/A2 rows stand. Local targeted run under `quiet light` pending.

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

1. Read the jobs; Result: idle CPU per relay and authority in core-seconds a day and watts, latency
   p50/p99 against the bands; paste the Evidence table; a correctness review.

## Blocked on

The lead: A1's authority regression. A parked next spins the authority's owner loop (~780 turns/s,
+3.65% of a core), more than the relay saves (1.04%). The cause is in src/detent_authority/
service.cc (a parked frames task counts as transient, 1 ms turns), outside this set. Options:
1. A quest (loop-idle-waits' shape) that wakes parked frames tasks on apply or their deadline;
   this quest lands after it, and A1's authority row is rerun then (needs your go). Recommended:
   agents parked at the authority pay the same spin today.
2. Widen this set to service.cc and fix it here (high risk, authority loop).
3. Land as is: relay -1.04% each, authority +3.65% while any relay or agent parks.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), name every job you examined, failures included (`campaign brief` lists failed
jobs no journal names), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| <A1> | <from `campaign brief`> |

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
