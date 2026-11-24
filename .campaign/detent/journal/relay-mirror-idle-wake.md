# relay-mirror-idle-wake: journal

State: working
Quest: .campaign/detent/quests/relay-mirror-idle-wake.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Baseline: A/A idle CPU at the default 250 ms (bench/idle-wakeups/compare.sh idle, branch = main +
bench edits only, against main).

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

1. Lead adds include/detent_relay/{mirror_service,namespace_relay,http_upstream}.hh and
   src/detent_relay/http_upstream.cc to the set (msg-310); then implement.
2. Counting test (A3) and its --fails-without at main; doc (A4); A1/A2 exclusive jobs.

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

<Decisions inside the quest's scope, so the reviewer can check them.>
