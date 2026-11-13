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
- The store adopts the profile a publication carries (`AgentStore::adopt`); a cycle under a new profile
  must drop or republish every entry, so no entry outlives the profile it was verified under.
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
- `first_read_export` writes its store with trusted_records::install and drops LAYOUT_VERSION (D43).
- `dense_reader_measure.py` stays as is: it measures preserved dense-pages stores (old layout) and needs
  the cursor identity, which the table no longer carries in clear (D88 hashes it); it is not in the suite.
- A2's crash model is T7 (`test_agent_store_crash`, a cut at every primitive of publish and collect,
  removals included): DiskStore's only directory I/O is AgentStore, so T7 cuts the agent's publish path.
  test_publication_power_cut (old layout) is deleted as superseded.
- A5 kills the publishing child at every AgentStore primitive (`--cut DIR K`, `_exit(0)`), then reopens
  and catches up; a kill leaves the same names a power cut leaves after the last sync, and T7 owns the
  unsynced-loss model.
- `--upgrade-managed` and UpgradeLegacy are deleted (D43, no migration).
- Mutants n6, f2 and s7-a2 are regenerated against the moved code; cor1-unguarded-parent and
  s3b-fu-a3-rebase-restarts do not apply on main either (pre-existing, the lead's).
- Deleted mutants of deleted code: s7-f2-orphan-remove-selected, a12-no-barrier, a12-no-refusal,
  s3b-fu-a2-missing-pack-fails-pass.
