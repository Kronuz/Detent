# namespace-s7-store: journal

State: working
Quest: .campaign/detent/quests/namespace-s7-store.md (the lead's; never edit it)

## Doing

Migrating tests/detent_local to the agent-store layout: hand-built stores go through
`tests/detent_local/trusted_records.hh` `install` (table, empty `.lease`, `.read`/`.live`
derived by one recovery); DiskStore drivers use `work()->run()` + `advance()`.

## Next

1. Fix the remaining tests/detent_local, detent_agent, detent_testing, detent_system compile errors
   (`.scratch/build-all.log`); keep root_reclaim, partial_reclaim and prepared_collection as DiskStore-level
   collection tests (A4).
2. Run test_agent_store, T7 (`test_agent_store_crash`, with collect() after each cycle) and the suite.
3. A5 catch-up on an AgentStore recovered from a mid-publish cut; A3 grep job; A4 mutant jobs.
4. docs/status/namespace-s7.md "## Wiring" (A6).

## Blocked on

msg-47 (to the lead): the file set lacks tests/detent_delivery/** (test_coordinator, bootstrap_retention,
bundle_maintenance, repair write DTSELECT/`.lease`/storage.meta or use UpgradeLegacy/operation()),
benchmarks/{paged_collection,paged_recovery,local_image,paged_snapshot}_bench.cc, tools/check_lineage.sh,
tools/w5/first_read.py and the layout docs. Without them the native check cannot be green.

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

<pending>

## Rulings I made

- DiskStore keeps its public API (begin, selection, Descriptor), so the relay needs no change.
- Disk work runs off the owner: `DiskStore::work()` hands one publish or collect to the coordinator's
  worker thread; `advance()` on the owner settles it.
- D88: the one table entry is named hex(scope); a publication drops every other entry (a reset that
  changes scope).
- The store's profile is fixed: the existing table's, or the default; another profile is refused.
- A flat image publishes an empty `.read` and a zero-row `.live` (read digest = SHA-256 of "").
- `.live` binds the SHA-256 of the whole `.read` (what the table holds), not `.read`'s own trailer:
  one digest for the store and trusted restart (`reader_metadata_digest`).
- PreparedGeneration views bytes (string_view); the caller keeps them alive, so packs are never copied.
- Readers take the Descriptor's cursor from an image peek (one more open and 320 bytes).
- The ledger's `used` is the AgentStore census (exact); a reservation settles against it.
- The old collector, inventory, storage session, certificates and segment marks are deleted; collection
  is AgentStore::collect() over `.live` reachability, with no directory sync.
- Tests of deleted modules are deleted (collector, inventory, record_reader, storage_session, publication,
  paged_certificate, paged_segment_{marks,read,collection}, segment_mark_hash, paged_named_root,
  paged_collection_{shutdown,crash,publication}); T7 with collect() covers collection cuts.
