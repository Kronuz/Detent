> Status: CURRENT (2026-11-24) how a relay's mirror waits for its upstream to change, read from `src/detent_relay/mirror_service.cc`, `src/detent_relay/http_upstream.cc`, `src/detent_authority/service_runtime.cc` and `src/detent_authority/namespace_sessions.cc`.

# Relay mirror wait

A relay's mirror (`MirrorService`) syncs only when its upstream may hold something new. Between
syncs it holds one W5 next parked at the upstream, which answers on a change or idle at its
deadline. An idle relay makes one parked request per half `timeout_ms` (5 s by default) and one
sync per `maximum_wait_ms` (30 s), instead of a sync every `interval_ms`.

## The wait

After each good sync the mirror thread hands the watcher thread one position per mirrored name:
the ring's head, or no cursor for a name the upstream does not hold. The watcher calls
`Upstream::wait(positions, maximum_wait_ms, stop)`; on a change or a failure the mirror thread
syncs: layouts, the files it lacks, and the entries after each head, as before.

`HttpUpstream::wait` sends W5's next with `wait_ms`, the protocol's own parked read; no endpoint
or field is new. The authority re-plans a parked next at every applied entry (a write, a seal, a
creation or a drop) and answers it when an answer has frames, or 204 at the deadline. A name it
does not hold yet gets no frame until it is created. A relay upstream parks the next until its
own sync moves past the cursor. The wait parks for `maximum_wait_ms`, bounded by the session's
`wait_ms` from its open and by half of `timeout_ms`, and the request's deadline is `timeout_ms`: a
source that answers nothing within the timeout hangs, and the relay reads past it.

A voter that knows no live leader (`serving()` false, as its `/v1/health` `ready`) answers its
nexts, parked or new, 503 `not_ready`, as it answers the history routes: a follower cut off from
the quorum releases a relay's wait once it notices, within `election_max`. An agent reads
`not_ready` as `Unavailable` and moves to its next source, as a relay does.

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
| A change: an answer with frames, or a 204 before the wait (a seal) | syncs and waits again from the new heads |
| Idle: 204 at the wait's deadline | waits again from the same heads; past `maximum_wait_ms` since the last good sync, syncs |
| A failure: no answer within the timeout, a refusal (503, 403), a restart | reads past that source (`skip`) and syncs; a failed sync backs off and arms no wait |

What the voter holding the wait does, and when the relay moves on:

| The voter | The relay's parked next | The relay |
|---|---|---|
| Serving, nothing changes | 204 at half the timeout at most | waits again at the same voter |
| Serving, a publish | 200 at once | syncs from that voter |
| Hung (stopped, wedged) | no answer | at the request's deadline, `timeout_ms`, reads past it |
| Cut off from its leader | 503 `not_ready` once it knows no live leader | reads past it at once |
| Restarting or gone | reset or refused at once | reads past it; failed syncs back off |
| Leader change, still in the quorum | stays parked; the new leader's entries answer it | stays |

A seal that moves no head (a write's seal, after its entries reached the relay) has no frame, so
a reader learns of it only from an early 204: the seal stamp below. The relay takes a 204 that
comes before its wait as a change: a deadline's 204 comes no sooner than the wait, measured from
the send, so the two cannot be confused. It syncs and fetches the sealed files, as polling did
within one interval.

## The seal stamp

Each W5 session, at the authority (`NamespaceSessions`) and at a relay (`MirrorService`), holds a
stamp: the upstream's seal at the session's last answer, or at its open. The authority's seal is
`sealed()->applied_index()`, read once per owner turn; a relay's is its mirror table's seal, which
only its own sync moves. Each visit of a next restamps its session (`restamp`), and a next whose
session's stamp was older answers 204 now, whether it just arrived or was parked. A reader's sync
always follows the answer that set its stamp, so a seal newer than the one its layouts read is
newer than the stamp too. A seal that lands after the sync's layouts read and before the next
parks therefore answers that next at once, instead of at `maximum_wait_ms`.

| Session \ event | Open | Next arrives | Seal applied | Answer (200, 204, error) | Close or expiry |
|---|---|---|---|---|---|
| None | stamp = the seal | 410 | none | none | none |
| Stamp = the seal, idle | none | frames: 200; else parks | stamp now older | stamp = the seal | the record and its stamp go |
| Stamp = the seal, a next parked | none | 409 `session_busy` | stamp now older: the parked next is visited and answers | stamp = the seal | the parked next is answered as any parked next; the record goes once it ends |
| Stamp older than the seal | none | frames: 200; else 204 at once | still older | stamp = the seal | the record and its stamp go |

The open is covered from the reader's side. A watch session opened after the sync's layouts read is
stamped with a seal that sync may not have read, so `HttpUpstream::wait` reports a change, without
parking, when it opens or reopens its session (at start, after a 410, after a move to another
source), and the relay syncs once more. Agents' nexts get the same early 204 once per seal, at most
one extra request per seal per session. No field or status is new: a 204 is always a valid answer
to a next.

A sync comes no sooner than `interval_ms` after the previous one, and a wait is asked no sooner
than `interval_ms` after the previous one, so a stream of changes or a short `timeout_ms` costs at
most one sync and one wait an interval, as polling did. A failed sync waits `interval_ms` doubled per
failure, up to `reconnect_maximum_ms`, and the next good sync arms the wait again.

An `Upstream` that cannot wait keeps the default `wait`, which returns at once: its mirror syncs
every `interval_ms`. The in-process upstreams in the tests do that. A relay that mirrors a name
its upstream refuses (403 on the next) degrades the same way.

## Tests

`tests/detent_relay/test_http_upstream.cc` (`idle`): relay B mirrors relay A over HTTPS with a
10 ms interval and a 4 s deadline. At rest B makes no sync in 1.5 s, where polling made 37
(job 261124-033951-a94472). A's change reaches B within 1.5 s, and B's next sync with nothing
changed comes at the deadline. A hook in B's upstream (`Hooked`) runs before a wait, so the steps
order A's seal against B's next without timing: a seal before B's watch session opens, a seal while
B's next is parked, and a seal after B's sync and before its next each reach B within 1.5 s. With A
stopped, B's syncs fail with the backoff, and B syncs again once A returns on its port. `tests/detent_relay/test_relay_hung.py`: three voters; when the voter
holding a relay's wait is stopped, the relay serves the quorum's publish within its 1 s timeout and
one sync (`hung`), and a follower whose two peers stop answers its parked next 503 (`cut-off`).
`test_http_upstream` also checks that a failed wait moves the mirror past its source before the
sync it wakes (`failed_wait`), and that a 400 ms timeout with a 1 s interval asks A at most once a
second (`paced`). `tests/detent_delivery/test_client_reset.cc` maps 503 `not_ready` to
`Unavailable`. `tests/detent_relay/test_relay_service.py` checks that relays reading a
three-voter authority, and a relay reading them, make no sync in 2 s at rest.
`tests/detent_relay/test_relay_idle.py`: with one voter and a relay at rest, the authority keeps to
its health turns (at most 60 a second) and the relay makes no sync over 2 s; a publish still wakes
the relay within 2 s. `tests/detent_relay/test_relay_seal_stamp.py` checks each transition of the
stamp table at the authority and at a relay: a next after the open parks, a second next answers
409, a next after a seal answers 204 at once and the one after it parks, a parked next answers when
the seal lands, and a closed session answers 410 while a new one is stamped at its open.
