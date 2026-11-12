# namespace-s6: journal

State: waiting on jobs 261112-122211-8ba8d6 261112-122223-334801
Quest: .campaign/detent/quests/namespace-s6.md (the lead's; never edit it)

## Doing

Code complete at Detent:13d2aa25 (D84 scope). Passed: pins 261112-122212-c1a91a, A2 grep
261112-122212-f2e389. Queued: native 261112-122211-8ba8d6 (--for A1-A5, the full ctest), A1 mutant
261112-122223-334801 (trim filter removed; the frames test must fail). Locally (`.scratch/build`,
not evidence): grants, session_frames, session_request, delivery_client, namespace_session pass.

## Next

1. When the jobs pass: paste `campaign brief`'s table, State "ready for review". A failure: read
   `campaign job log ID`, fix, rerun.

## Blocked on

Nothing. (The file-set blocker of Detent:8a95bbfb is resolved by D84.)

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | <no evidence yet> |
| A2 | <no evidence yet> |
| A3 | <no evidence yet> |
| A4 | <no evidence yet> |
| A5 | <no evidence yet> |
| A6 | <no evidence yet> |

## Result

The library half of S6 (D84), proven against a test history source and a test server; the
authority, relay and old-wire removal are namespace-s6-cutover's and namespace-s8's.

- W1: `NamespaceCursor` and `DirectoryCursor` in `include/detent/cursor.hh`; `agent_store.hh`'s copies
  are gone (it re-exports the shared types).
- History interface (S5.2's contract): `include/detent_distribution/history.hh`, seven calls
  (head, cut, ring_start, spans, read_span, read_ring, read_full_copy) and `select_answer`.
- W6: `include/detent_distribution/session_frames.hh`, a bounded-step encoder and a checking decoder;
  level-0 sections merged at answer time; bodies framed as `UpdateKind::Frames` fragments.
- W5: version 2 of `parse_session_request`.
- W13: `NamespaceGrants` (exact, `p.*`, `*`).
- Agent client: `include/detent_delivery/namespace_session.hh` (open, next, close, frames decoder).
- Bytes per answer on the test churn (40 positions, cut 8, ring from 30): full copy 545 B / 22
  records; blocks from the cut 835 B / 38 falling to 445 B / 17 at R = 29; entries 414 B / 15 at
  R = 30 to 73 B / 1 at R = 39; golden blocks answer 76 B (all pinned in
  `tests/detent_distribution/test_session_frames.cc`).
- Docs: `docs/status/namespace-s6.md` (grammar, tiers, checks, responses).

## Rulings I made

- R1 Names: the W1 type is `NamespaceCursor` (the old global `Cursor` still holds the name until the
  cutover deletes it); the directory's is `DirectoryCursor`. `agent_store.hh` keeps
  `using DirectoryPosition = DirectoryCursor` because `agent_store.cc` (okt-followups' set) spells it
  so; the cutover drops the alias.
- R2 Grants: `p.*` matches names strictly below `p.` at any depth, never `p`; '.' is the only
  component separator; patterns are bounded at 4,096.
- R3 History contract: ring records come by position then key, each key once per position; unnamed
  spans (level-0) are always the newest; a broken promise is `HistoryFailure` (source failure).
- R4 Tiers: a store cursor carrying the namespace's identity at position 0 is an empty store of that
  incarnation (entries or blocks); no store at all gets a full copy (D6 tier 3).
- R5 Frames: records end with nil instead of a count, so a trimmed block or merged level-0 streams
  in one read; a whole named block is piece kind 2 only when it starts at R + 1; a full copy frame
  has `after` 0 and piece [0, s]; frames come in name order. S6 owns the record encoding S5.2's
  block files can reuse, so a stored block and a sent one hold the same record bytes.
- R6 W5: the cluster is sent once per request; a store of another cluster is sent as `[name]`
  (both earn the same full copy); names strictly increasing; version 1 stays until the cutover.
- R7 Integrity: W6 bodies ride the existing DTDIST01 fragments (`UpdateKind::Frames`), keeping
  per-fragment SHA-256 (D12.2) and zstd.
- R8 Admission: the new encoder, decoder and client allocate from the heap, not a `StorageLedger`;
  the cutover, which puts them on the authority's and agent's hot paths, admits them like the v1
  codecs. The status-to-reset mapping and the transport checks are one shared function each
  (`src/detent_delivery/session_control_internal.hh`), used by both clients.
- R9 Out of scope here (D84): `delivery_mode` and the required `namespaces` in managed.json go with
  the cutover, which wires the coordinator to `namespace_session`.
