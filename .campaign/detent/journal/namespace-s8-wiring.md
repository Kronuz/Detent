# namespace-s8-wiring: journal

State: working
Quest: .campaign/detent/quests/namespace-s8-wiring.md (the lead's; never edit it)

## Doing

End-to-end test tests/detent_relay/test_relay_service.cc (A1/A2/A3) over real detentd voters and two
detent-relay processes (A reads the voters, B reads A).

## Next

1. Write test_relay_service.cc and its CMake entry (fixture authority_tls), build, run locally.
2. A3 test: member routes 404 on the relay, `voters` config refused, no consensus symbols.
3. docs/status/namespace-s8.md "The service" section (A4).
4. Submit jobs (check + A1..A3 + mutants), set State ready for review.

## Blocked on

Nothing.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | pending |
| A2 | pending |
| A3 | pending |
| A4 | pending |

## Result

Pending.

## Rulings I made

1. One layout encoding (DTMIRR01, sequence 0) serves the mirror's table and the HTTP layouts answer;
   the codec moved to detent_relay_layout so the authority links it without mirror I/O.
2. `Upstream::fetch` and `MirrorFetch` carry the namespace, so every upstream authorizes a file read by
   W13 grant and serves only files its current layout names for that namespace.
3. New authority routes `POST /v2/history/layouts` and `/v2/history/file`, answered on the owner from
   the voter's own sealed history (no quorum read: files are immutable, the layout is the voter's).
   They want positions like a W5 next, so a relay alone triggers the enabling entry.
4. A lagging voter's older layout for the same incarnation is refused (`UpstreamBehind`), never adopted.
5. The relay refuses `all` (400) and names it does not mirror or grant (403); nexts get 503
   source_behind before the first sync; history routes share the opens lane.
6. A relay may run with no v1 bundles when a mirror is configured; credentials may carry W13
   `namespaces` beside or instead of `bundles`.
