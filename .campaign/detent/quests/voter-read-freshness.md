# voter-read-freshness: Voters serve history only after a leader-confirmed read

Risk: high. The lead writes this quest; contributors read it and never edit it. Its machine state
(status, branch, checks, reviews) is in `state.json`: `campaign status voter-read-freshness`.

## Outcome

A voter answers the history and layouts routes (W5 layouts, files) only from state it has confirmed current with the leader for that request (a ReadIndex-style round: the leader confirms its leadership with a quorum and names a commit index, and the voter answers once it has applied that index), so a relay or reader never adopts from a voter cut off from its leader, however briefly. This closes D11 of namespace-s8-wiring, which the readiness gate there narrows to at most the election timeout (D101).

## Acceptance criteria

Three to five, at outcome level: `- A1 <criterion>: <the test that shows it>`. An untagged one
needs a job tagged `--for A1` passing at the landed code, often the suite; tag one `[artifact]`
(a committed `path`), `[decision]` (a PLAN.md Decisions id) or `[manual]` (checked by hand) when no
job can show it.

- A1 [decision] The read-confirmation design (where the round runs, its cost per request and per relay sync, batching, behavior with no leader, interaction with the D1 seal and the D9 source skip) is reviewed before code: a `design` review and its Decisions row
- A2 A voter cut off from its leader (stopped leader, partition, resumed-after-stop voter with queued frames) never answers history or layouts with state the quorum has moved past; relay D's A5 cases in `test_relay_service` (start alone, at the relay's seal) pass 20 of 20 under CPU load, and a mutant that skips the confirmation is killed
- A3 The cost is measured: relay sync latency and voter CPU per history request before and after, exclusive jobs, against an A/A band
- A4 [artifact] `docs/status/namespace-s8.md` and the authority HTTP reference describe when a voter answers history

## Contracts

Consumes consensus's read path (or adds a ReadIndex primitive to it, then a quest in Kronuz/consensus first). Keeps the W5/W6 wire format; a not-yet-confirmed voter answers 503 `not_ready`, which relays already skip (namespace-s8-wiring D9/D11).

## Decisions

D101 (owner, 2026-11-16): split from namespace-s8-wiring. Open, for the design review: whether W5 `next` (reader-facing) is gated the same way (namespace-s8-wiring ruling 1 left it ungated).

## May land early

None.
