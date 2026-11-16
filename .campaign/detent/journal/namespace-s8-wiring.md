# namespace-s8-wiring: journal

State: ready for review (waiting on jobs 261116-005109-0f20c5, 261116-005055-6d0652, 261116-005056-465976, the mutants and probe below, baseline 261116-004118-3871c0)
Quest: .campaign/detent/quests/namespace-s8-wiring.md (the lead's; never edit it)

## Doing

Nothing: the jobs below remain.

## Next

1. When the jobs end, record their results here; the suite job 261116-005109-0f20c5 confirms D1 and D2 (`campaign accept` is the lead's); D3 is `check_docs.py` locally and the Docs CI once pushed.
2. If native 261116-005055-6d0652 fails only the four system tests, compare with baseline 261116-004118-3871c0 at main.

## Blocked on

Nothing.

## Evidence

Durability review (CHANGES) folded in at Detent:e09d3690 (code at Detent:bfb6a386, docs Detent:8da51b76,
mutants Detent:9945b0f0; jobs submitted at Detent:e09d3690).

Local runs at the fix (Release, loaded host): `test_relay_mirror` 10 s (incl. `behind`: 6 older upstreams
refused across a restart), `test_relay_chain` 44 s (each voter's export equals its oracle at every step;
an unsealed drop leaves it unchanged), `test_relay_service` 49 s (incl. the busy proxy).

| Criterion | Evidence |
|---|---|
| A1 | suite 261116-005109-0f20c5 (`test_relay_service`, `test_relay_mirror`, `test_relay_chain`, `test_relay_mirror_crash`); mutants 261116-005111-5eb137 (`older-seal-adopted`), 261116-005113-852cae (`same-seal-adopted`), 261116-005128-226477 (`busy-not-retried`), 261116-005130-cf3b1c (`layouts-no-enable`), 261116-005133-947e51 (`upstream-drops-frames`), 261116-005123-e69b7a (`hide-failed`), 261116-005125-7e45d6 (`keep-dropped`), 261116-005116-bf5989 (`fetch-reached`), 261116-005118-7f277f (`no-collector`), 261116-005121-6ea6c5 (`trust-first-read`) |
| A2 | suite 261116-005109-0f20c5; mutant 261116-005135-178dd4 (`mirror-forgotten`) |
| A3 | suite 261116-005109-0f20c5; mutant 261116-005138-b4f850 (`member-route-admitted`) |
| A4 | [artifact] `docs/status/namespace-s8.md`, "The service" (updated: seal order, busy retry, 413, backoff) |
| checks | native 261116-005055-6d0652, pins 261116-005056-465976 |

Review findings:

| ID | Fix | Test | Fails without |
|---|---|---|---|
| D1 | A layouts answer carries its seal (DTMIRR01 header word 4), built from the sealed state alone (layout_export.cc); the mirror table records its newest seal; `attempt` refuses an older seal, a lower or disagreeing same-seal layout, and a drop of a name held at that seal (namespace_relay.cc) | `test_relay_mirror` `behind` (the probe's scenarios plus same-incarnation, across a restart), `test_relay_chain` export checks; probe rerun 261116-005140-01fe78 (adapted to the new `Upstream::layouts` type: one line) | probe 261115-234744-05b4d6 exits 1 at d77c332d; mutants `older-seal-adopted`, `same-seal-adopted` |
| D2 | `HttpUpstream::frames` keeps the session on 409 `session_busy` and asks again, 10 times, 1 ms doubling | `test_relay_service` reads the leader through a TLS proxy that answers every other next 409 `session_busy` | mutant `busy-not-retried` |
| D3 | reference/configuration.md (mirror keys, `application_limits.layout`), reference/http-api/authority.md (routes, `history_unavailable`, 413, `session_busy`); stale `application_limits` exception removed | `check_docs.py`: 43 gaps, 43 listed, 0 unlisted, 0 stale | Docs run 43820854964 at d77c332d |
| D4 | `LayoutExport::file` refuses a file above `reply_bytes - 256` with 413 `reply_bound`, before reading when a layouts answer sized it; the mirror thread doubles its wait after each failed sync, up to 30 s or one interval | `test_relay_chain` (length_error at size - 1, bytes at size) | none: the post-read bound also throws, so a mutant of the pre-read check survives; the saving is the read, not the answer |

Re-review (CHANGES, D9 and D10 blocking), the quest's last fix round:

| ID | Fix | Test | Fails without |
|---|---|---|---|
| D9 | `Upstream` gains `sources()` and `skip()`; `NamespaceRelay::sync` on `UpstreamBehind` skips to the next source and starts over, each source once per sync, then throws (the service backs off as before); `HttpUpstream` answers its endpoint count and rotates | `test_relay_service` relay C reads `[lagging proxy, leader]`: the proxy forwards until C holds the authority's seal, then answers every layouts request with the first one it forwarded; C converges with the authority after 12 more writes, and the proxy served a stale answer | mutant `behind-not-skipped` (rethrow at once) |
| D10 | `test_relay_service` checks the authority's heads hold every name at a position > 0, that each name's answers at 0 and at its head differ, and that the answer at no cursor read at the authority, A and B with the test's own HTTP and DTDIST01 parsing (no HttpUpstream) equals the probe's | the same | mutant `upstream-drops-frames` |

The earlier failed job 261116-005133-947e51 (listed under A1 without a note) was that mutant
surviving, not a flake: every 200 next read as an empty body, so the probe reached no head, asked
only the two `*` answers, and they were empty and equal everywhere (the review's rerun
261116-023219-318dfb logs `heads {}`, 2 answers). Its kill at 591511ce (261115-230049-08c459) was
an unrelated 404. D10's checks make the comparison hold frames read without the code under test.

D5-D8 (nits): D6 fixed (relay history routes answer 403 `scope_forbidden`), D7 fixed (only a layouts
request sets `history_wanted`). D5 (history routes skip the cluster/group/epoch context check) and D8
(`free_port` bind-and-close race in `test_relay_service`; the new proxy binds port 0 itself) are left for
namespace-followups.

## Result

detent-relay serves its namespace mirror over HTTPS (W8). `MirrorService` (src/detent_relay/mirror_service.cc)
owns `NamespaceRelay` on one thread, syncs from `HttpUpstream` (src/detent_relay/http_upstream.cc), and
answers W5 version 2 and the history routes; version 1 stays with the bundles. The authority adds
`POST /v2/history/layouts` and `/v2/history/file` (src/detent_authority/layout_export.cc) and takes
`application_limits.layout`. Layouts are ordered by the upstream seal they carry, so a relay never follows a
lagging voter backwards. How it works and the routes in Contracts terms: docs/status/namespace-s8.md,
"The service"; reference: website/src/content/docs/reference/{configuration,http-api/authority}.md. Left
open there: streamed fetch of large files (a file above the reply bound is refused 413), a bound on the ring.

## Rulings I made

1. One layout encoding (DTMIRR01) serves the mirror's table and the HTTP layouts answer; the codec moved
   to detent_relay_layout so the authority links it without mirror I/O. An answer is a table with
   sequence 0 and `seal` the upstream seal it was built at; the mirror's table keeps `sequence` as its own
   write count and `seal` as the newest seal it adopted.
2. `Upstream::fetch` and `MirrorFetch` carry the namespace, so every upstream authorizes a file read by
   W13 grant and serves only files its current layout names for that namespace.
3. New authority routes `POST /v2/history/layouts` and `/v2/history/file`, answered on the owner from
   the voter's own sealed history (no quorum read: files are immutable, the layout is a function of the
   seal). A layouts request wants positions like a W5 next, so a relay alone triggers the enabling entry.
4. (Replaces the same-incarnation rule.) A relay refuses any answer older than its mirror: a seal below
   the table's, a layout below the one held, another layout at the same seal, or a name absent at a seal
   where the mirror holds it (`UpstreamBehind`). `MirrorStore::adopt` takes the seal and refuses a lower
   one (std::invalid_argument) as a guard.
5. The relay refuses `all` (400) and names it does not mirror or grant (403); nexts get 503
   source_behind before the first sync; history routes share the opens lane.
6. A relay may run with no v1 bundles when a mirror is configured; credentials may carry W13
   `namespaces` beside or instead of `bundles`.
7. The authority's configuration takes `application_limits.layout` (validated as SetLayout), so a
   process-level test reaches bases, blocks and sections; additive and optional.
8. The relay's file route answers only files its current layout names (404 otherwise); a layouts
   request from the relay lists only the names it mirrors and the credential grants.
9. `test_relay_chain` links `detent_authority_service` to check `LayoutExport` against its oracle.
10. Out-of-set files for D3 (msg-58, msg-59 to the lead): the two website reference pages and
    tests/docs/docs_exceptions.txt. Six stale mutant patches (four already stale at d77c332d) are
    regenerated with the same mutation.
11. `test_relay_service.py` is ruff-formatted and lint-clean (three `check=False`).
