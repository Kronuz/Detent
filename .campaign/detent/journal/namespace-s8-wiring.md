# namespace-s8-wiring: journal

State: folding in the durability review (D1-D4 fixed in the tree, building)
Quest: .campaign/detent/quests/namespace-s8-wiring.md (the lead's; never edit it)

## Doing

Durability review (CHANGES) fixes: D1 seal-ordered layouts, D2 busy retry, D3 docs, D4 413 and backoff.

## Next

1. Build and run test_relay_mirror, test_relay_chain, test_relay_service locally; commit.
2. Refresh the mutant patches; submit suite, mutant and probe jobs at the new head.

## Blocked on

Nothing.

## Evidence

Local run (Release, loaded host): `test_relay_service` passes in 45 s: 12 files (base, block, section)
and 111 W5/W6 answers byte-identical at the leader, relay A and relay B; after a restart both fetched
0 files (12 reused) and answered unchanged; member routes 404.

| Criterion | Evidence |
|---|---|
| A1 | job 261115-230037-c02ee6 (`test_relay_service`, plus `test_relay_mirror`, `test_relay_chain`); mutants 261115-230043-562639 (`layouts-no-enable`), 261115-230049-08c459 (`upstream-drops-frames`) |
| A2 | job 261115-230037-c02ee6; mutant 261115-230054-1ddbbb (`mirror-forgotten`) |
| A3 | job 261115-230037-c02ee6; mutant 261115-230058-44d37f (`member-route-admitted`) |
| A4 | [artifact] `docs/status/namespace-s8.md`, "The service" |
| checks | native 261115-230026-744f5e, pins 261115-230026-c0f570 |

## Result

detent-relay serves its namespace mirror over HTTPS (W8). `MirrorService` (src/detent_relay/mirror_service.cc)
owns `NamespaceRelay` on one thread, syncs from `HttpUpstream` (src/detent_relay/http_upstream.cc), and
answers W5 version 2 and the history routes; version 1 stays with the bundles. The authority adds
`POST /v2/history/layouts` and `/v2/history/file` (src/detent_authority/layout_export.cc) and takes
`application_limits.layout`. How it works and the routes in Contracts terms: docs/status/namespace-s8.md,
"The service". Left open there: streamed fetch of large files, a bound on the ring.

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
7. The authority's configuration takes `application_limits.layout` (validated as SetLayout), so a
   process-level test reaches bases, blocks and sections; additive and optional.
8. The relay's file route answers only files its current layout names (404 otherwise); a layouts
   request from the relay lists only the names it mirrors and the credential grants.
