# dd3-full-copy-pages: journal

State: blocked on lead: the agent's full-copy path is v1, not W6 (see Blocked on)
Quest: .campaign/detent/quests/dd3-full-copy-pages.md (the lead's; never edit it). Read at Detent:b26dad5c (A5 added).

## Doing

Nothing until the lead rules on the wire the transfer rides (Blocked on).

## Next

1. On the ruling: merge DD1 (quest/detent/dd1-transfer-receiver, header `src/detent_local/paged_transfer.hh` at Detent:1e410613), then build the chosen path.

## Blocked on

The lead: which wire carries the full copy as pages, and the file set for it. DD3 as written
(dense-distribution.md DD3, D1, D8) puts the encoding and target commitment in namespace W6's
frame header and has the agent receive the transfer from a W6 frame. On main (Detent:217596ad)
no agent receives W6 frames:

1. The agent's coordinator speaks v1 only: `open_session` with `DeliveryMode::Subscription`
   (`src/detent_delivery/coordinator.cc:983-986`), `ReceivedUpdate` snapshot/delta, built by
   `PagedSnapshotPlanBuilder` (`coordinator.cc:394`). `FramePreparer`, the W6 consumer, is
   implemented only in `tests/detent_delivery/test_namespace_session.cc`.
2. The relay's copy to agents is the v1 bundle: `Bundle::snapshot_step` re-encodes its head
   generation as records (`src/detent_relay/bundle.cc:205-266`, `make_snapshot_source(*head_)`)
   into the update cache. `host.cc` only drives it.
3. The relay's W6 side (namespace mirror, S8) holds records and computes no layout or pages
   (docs/status/namespace-s8.md "What the mirror is"), so it has no generation to ship (D7.2).
4. Moving the agent to v2 sessions is namespace-s6-v1-removal (planned, after
   namespace-local-cursor; D93: est. 3-5K lines over five libraries).
5. A v1 generation is one tree holding the scope's namespaces (`paged::Root::namespaces`), not
   W10's one root per namespace, so A2's per-namespace bytes need one namespace per bundle in
   the fixture on v1.

Options:

- (a) Build DD3 on today's v1 path. The relay bundle serves its head generation as a transfer
  (make_transfer; DD2's relocation when its files are not canonical) for a snapshot answer; the
  v1 snapshot header (`protocol.cc`) gains the encoding and the 32-byte commitment; the agent's
  coordinator runs DD1's receiver and publishes the segments at derived ids, ordinal 0; a refusal
  re-asks for records. Measurable at 100K/5M cold bootstrap now (A1, A5). Needs these files
  added: `src/detent_relay/bundle.cc`, `include/detent_relay/bundle.hh`,
  `src/detent_distribution/{encoded_update,update_cache,snapshot_encoder}.cc`,
  `src/detent_delivery/{client,response}.cc` and their headers, and a local publication of a
  received transfer (`src/detent_local/` new file plus `disk_store.cc`). Cost: the v1 binding
  is ported again by namespace-s6-v1-removal (the receiver and publication carry over; only the
  header and the bundle hook move).
- (b) Wait for namespace-s6-v1-removal, then build DD3 as written on W6. One shape, no port;
  DD3 slips by that quest (not started, 3-5K lines), past D133's day.
- (c) Build on W6 now between the relay mirror and the test-only agent client: needs a page
  build on the mirror (D7.2, new) and gives no production cold bootstrap to measure A1/A4/A5.

Recommendation: (a). It is the only path that meets D133's day and A5's measured win; the
design's mechanism (transfer, receiver, canonical copy, fallback) is wire-agnostic, and the part
that is v1-specific is the header fields and the bundle hook, which v1 removal rewrites anyway.
If the lead takes (a), the design deviates in one place: the commitment rides the v1 snapshot
header, not W6's (record as a Deviation once ruled).

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | <no evidence yet> |
| A2 | <no evidence yet> |
| A3 | <no evidence yet> |
| A4 | <no evidence yet> |
| A5 | <no evidence yet> |

Owner message (2026-11-20, via the lead): build the design as written; deviate only on a
measurement, under "Deviation". A5 acknowledged: if the page transfer does not greatly beat the
records arm (CPU and wall, exclusive), stop and report the numbers instead of landing.

## Deviation

None yet. The Blocked-on item is a missing prerequisite, not a measurement against the design.

## Result

<none yet>

## Rulings I made

None yet.
