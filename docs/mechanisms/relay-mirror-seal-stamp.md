# Relay mirror: the seal stamp

Status: design, for the lead's design check. It replaces the seal answer in
[relay-mirror-wait.md](relay-mirror-wait.md) (`seal_seen` and `was_parked` in the authority,
`resealed` in the relay's `revisit`).

## The miss it closes

A seal that moves no head has no frame, so a reader learns of it only from an early 204. Today an
upstream sends that 204 only to the nexts parked when it applies the seal. A relay's wait is not
parked between its sync's layouts read and the moment its next reaches the upstream. A seal applied
in that window answers nothing: the next parks at unchanged heads and the relay adopts the seal at
`maximum_wait_ms` (30 s), where polling adopted it within one interval.

CI run 45542785995 (linux-tsan, linux-asan-ubsan) shows it: `test_http_upstream` `idle` reseals A
right after B's deadline sync. B's next reached A after A had adopted the seal, so B saw the seal
at its next deadline, 4106 ms and 4115 ms after it, against a bound of 1.5 s. The change step in
the same runs took 57 ms and 210 ms. The window is one round trip of B's next, so release builds
on an idle machine rarely hit it (correctness review R2-4: `seal_race.py` 0 of 80).

## The stamp

Each frames session records the upstream's seal at its last answer: the authority's
`sealed()->applied_index()`, or the relay's mirror table seal. A next on a session whose stamp is
older than the current seal answers 204 at once, whether it is new or parked. A reader's sync
always starts after the answer that set its stamp. So any seal newer than the one its layouts read
is also newer than the stamp, and the next answers 204 at once.

| State of the session \ event | Session open | Next arrives | Seal applied (owner turn) | Answer 200 or 204 | Session ends or expires |
|---|---|---|---|---|---|
| No session | stamp = current seal | 410, as today | none | none | none |
| Stamp = current seal, idle | none | plan it: frames answer 200, else park | stamp now older | stamp = current seal | the stamp goes with the record |
| Stamp = current seal, parked | none | 409 `session_busy`, as today | stamp now older; the turn visits the parked next | stamp = current seal | the parked next answers as today; the stamp goes with the record |
| Stamp older than the seal | none | frames answer 200; else 204 at once | still older | stamp = current seal | the stamp goes with the record |

What is watched: the upstream's current seal, read once per owner turn (the authority) or per sync
(the relay). What each lookup answers: whether the session has had an answer since the latest
seal. The stamp is a field of the session record, so it has no lifecycle of its own.

The reader side covers the stamp set at open. A session opened after the sync's layouts read is
stamped at open, so a seal in between would be missed. `HttpUpstream::wait` therefore reports a
change when it opens or reopens its watch session (after a 410), instead of parking, and the relay
syncs once more. That costs one extra sync per watch session: at start, after a session's maximum
lifetime, and after a rotation.

## Cost

Agents' nexts get the same early 204 per seal that a parked next gets today (R2-5): at most one
extra request per seal per session. There is no format change and no new field: a 204 is always a
valid answer to a next.

## Files

- `src/detent_authority/namespace_sessions.hh`, `namespace_sessions.cc`: the stamp in
  `NamespaceSessions::Session` (outside the quest's file set).
- `src/detent_authority/service_runtime.cc`: the stamp read and set around each next, in place of
  `seal_seen` and `was_parked`.
- `src/detent_relay/mirror_service.cc`: the stamp in the relay's `Session`, in place of `resealed`.
- `src/detent_relay/http_upstream.cc`: a wait that opens its session reports a change.

## Tests

- `test_http_upstream` `idle`: B's wait is wrapped in a 100 ms delay, and the test reseals A right
  after B's sync. B must sync the seal within 1.5 s. It fails without the stamp (B waits for its
  4 s deadline) and without the open rule.
- An authority test, in a reader's order against one voter: write, then take the next's 200; once
  the write's seal applies, send the next at the new heads. It must answer 204 at once, and it parks
  without the stamp.
- `seal_race.py` reruns at the fix with a delay injected after each sync.
