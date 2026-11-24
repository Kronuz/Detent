# relay-mirror-idle-wake: journal

State: waiting on jobs 261124-034543-d72b56 261124-034500-c0335d 261124-034503-387342
Quest: .campaign/detent/quests/relay-mirror-idle-wake.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Waiting on the lead (msg-310, file set) and jobs. The fix is written in the worktree but its
out-of-set files are uncommitted: src/detent_relay/mirror_service.cc (in set, uncommitted since it
needs the headers), include/detent_relay/{mirror_service,namespace_relay,http_upstream}.hh,
src/detent_relay/http_upstream.cc. Probe commit c97932cb (dangling, made with `git commit-tree`;
holds exactly that diff) runs the relay tests: probe 261124-034543-d72b56 (no evidence).
Baselines at b06a8102 (main's binaries) vs main 24c5601a (no src change since the branch base):
A/A idle 261124-034500-c0335d, A/A latency 261124-034503-387342, both exclusive, queued.

Smoke 261124-033656-d26b03 (main's code, 5 s, 5 samples): idle relay 1.56% of a core, 4.36
syncs/s, authority 2.34% at 250 ms; latency p50 145 ms.
A3 fails-without 261124-033951-a94472 at b06a8102 (main's mirror + the idle test): exit 8 at the
first check, "B synced 37 times in 1.5 s at rest" (passes as --fails-without).

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

1. On msg-310's answer: commit the fix (mirror_service.cc and the four out-of-set files), then
   `campaign verify --for A1`(suite) and A1/A2 exclusive head-vs-main jobs; push.
2. CHANGELOG line; Result with numbers; core-seconds a day and power.

## Blocked on

The lead: file set (msg-310). In-set work continues meanwhile.

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
