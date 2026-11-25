> Status: CURRENT (2026-11-24) how a relay's mirror waits for its upstream to change, read from `src/detent_relay/mirror_service.cc` and `src/detent_relay/http_upstream.cc`.

# Relay mirror wait

A relay's mirror (`MirrorService`) syncs only when its upstream may hold something new. Between
syncs it holds one W5 next parked at the upstream, which answers on a change or at the wait's
deadline. An idle relay makes one request per deadline, 30 s by default, instead of a sync every
`interval_ms`.

## The wait

After each good sync the mirror thread hands the watcher thread one position per mirrored name:
the ring's head, or no cursor for a name the upstream does not hold. The watcher calls
`Upstream::wait(positions, maximum_wait_ms, stop)`, and when it returns the mirror thread syncs:
layouts, the files it lacks, and the entries after each head, as before.

`HttpUpstream::wait` sends W5's next with `wait_ms`, the protocol's own parked read; no endpoint
or field is new. The authority re-plans a parked next at every applied entry (a write, a seal, a
creation or a drop) and answers it when an answer has frames, or 204 at the deadline. A name it
does not hold yet gets no frame until it is created. A relay upstream parks the next until its
own sync moves past the cursor. The wait is `maximum_wait_ms` bounded by the session's `wait_ms`
from its open, and its HTTP deadline is `timeout_ms` plus that wait.

A parked next costs the authority no turns. Its owner loop marks a next it has looked at for the
current applied index as parked, not as work in flight, and sleeps until its health turn or the
next's deadlines; a publish wakes the owner, which re-plans every parked next at the new index
(`service_runtime.cc`, `frames_busy` and `frames_until`). A next it has not looked at, or one waiting for
history to be ready, keeps the 1 ms turn.

The wait runs on its own W5 session at the source the last sync read, so it never makes the
mirror's session busy, and the mirror thread keeps serving downstream reads and fetching files
while it is parked. Only the watcher touches that session, and the counters are atomic. A stop
(`MirrorService::close`) requests the wait's `std::stop_token`, whose callback closes the parked
connection, so a relay stops at once rather than at the deadline.

## What wakes a sync

| The wait returns on | The mirror then |
|---|---|
| A change: an answer with frames | syncs and waits again from the new heads |
| The deadline: 204 | syncs, adopting a seal that moved no head, and waits again |
| A failure: no answer, a refusal, a restart | syncs; a failed sync backs off and arms no wait |

A sync comes no sooner than `interval_ms` after the previous one, so a stream of changes costs
at most one sync an interval, as polling did. A failed sync waits `interval_ms` doubled per
failure, up to `reconnect_maximum_ms`, and the next good sync arms the wait again.

An `Upstream` that cannot wait keeps the default `wait`, which returns at once: its mirror syncs
every `interval_ms`. The in-process upstreams in the tests do that. A relay that mirrors a name
its upstream refuses (403 on the next) degrades the same way.

## Tests

`tests/detent_relay/test_http_upstream.cc` (`idle`): relay B mirrors relay A over HTTPS with a
10 ms interval and a 4 s deadline. At rest B syncs at most once in 1.5 s, where polling made 37
(job 261124-033951-a94472). A's change reaches B within 1.5 s, and B's next sync with nothing
changed comes at the deadline. With A stopped, B's syncs fail with the backoff, and B syncs again
once A returns on its port. `tests/detent_relay/test_relay_service.py` checks that relays reading a
three-voter authority, and a relay reading them, make no sync in 2 s at rest.
`tests/detent_relay/test_relay_idle.py`: with one voter and a relay at rest, the authority keeps to
its health turns (at most 60 a second) and the relay makes no sync over 2 s; a publish still wakes
the relay within 2 s.
