# relay-mirror-idle-wake: journal

State: waiting on jobs 261124-035147-903054 261124-035149-ce2080 261124-035150-981777 261124-035152-3ac6f3 261124-035154-314fca 261124-035211-92ed4b 261124-034503-387342
Quest: .campaign/detent/quests/relay-mirror-idle-wake.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

msg-310: the lead widened the set; the fix is committed (e2f92d48), main merged (80393d76, no
src change), branch pushed. Dangling probe c97932cb and its job 261124-034543-d72b56 cancelled.
Queued at 80393d76 against main ba79caae:
- A3 fixed side: 261124-035147-903054 (test_http_upstream). Fails-without at main: 261124-033951-a94472
  (exit 8, "B synced 37 times in 1.5 s at rest").
- A5 suite: 261124-035149-ce2080.
- A1 idle CPU relay+authority at 250 ms, 3 rounds interleaved, exclusive: 261124-035150-981777;
  A/A band (main twice, at b06a8102): 261124-035211-92ed4b (the first A/A, 261124-034500-c0335d,
  was superseded by the A1 job and cancelled).
- A2 latency p50/p99, exclusive: 250 ms 261124-035152-3ac6f3, 10 ms 261124-035154-314fca; A/A
  latency band 261124-034503-387342.

Smoke 261124-033656-d26b03 (main's code, 5 s): idle relay 1.56% of a core, 4.36 syncs/s,
authority 2.34% at 250 ms; latency p50 145 ms.

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

Nothing (msg-310 answered: the set now holds the headers and http_upstream.cc).

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), name every job you examined, failures included (`campaign brief` lists failed
jobs no journal names), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| <A1> | <from `campaign brief`> |

## Result

<When ready for review: what changed, its measured effect with numbers and the jobs behind them,
and what it leaves open. Results and how it works go in the docs of the repository it changed
(docs/perf/, docs/mechanisms/, a library's own); name them here. `campaign map` shows this.>

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
