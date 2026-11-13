# namespace-s7-store: journal

State: ready for review; jobs 261113-044509-c22720 (suite) and 261113-044510-08f77b (T7) still running; native check blocked on msg-47/msg-48
Quest: .campaign/detent/quests/namespace-s7-store.md (the lead's; never edit it)

## Doing

Evidence jobs at Detent:518f6147 (the head's code). Earlier runs found two issues, fixed:
test_paged_publication_intents collected before the base view's `.lease` closed on the retirement
thread (flaky; it now waits), and the reader's table bytes were counted twice (CI's counters
configuration, job 261113-042437-89be6e: got 1746, want 1129).

## Next

1. Read jobs 261113-044509-c22720 and 261113-044510-08f77b; fix and rerun if either fails.
   Passed at the head: A3 261113-044510-384de8, A4 261113-044511-bfb064, counters 261113-044512-76bfe6,
   pins 261113-044512-6794a6.
2. When the lead widens the file set (msg-47/48): port tests/detent_delivery's test_coordinator,
   test_repair, test_bootstrap_retention, test_bundle_maintenance and test_managed_process.sh to
   trusted_records and the table, then `campaign verify` for a green native check.

## Blocked on

- msg-47 and msg-48 (to the lead, unanswered): the file set lacks tests/detent_delivery/**. Four tests
  there do not compile against the deleted layout (test_coordinator, test_repair,
  test_bootstrap_retention, test_bundle_maintenance) and test_managed_process.sh cmp's `storage.meta`
  (8 ctest entries); plus benchmarks/*_bench.cc, tools/check_lineage.sh, tools/w5/first_read.py,
  docs/mechanisms/change-file.md (says a fresh `change` is 16 bytes; it is 8 until the first
  publication) and the layout docs. The native check cannot be green until they move.
  Recommendation: widen to tests/detent_delivery/** and those files; this quest ports them.

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | job 261113-044509-c22720: the suite at the head, less the 12 entries outside the file set (msg-47, msg-48); test_system_managed_* run authority, relay and agent and compare readers' answers |
| A2 | job 261113-044510-08f77b: T7, a cut at every primitive of publish, repair, collect (each removal) and startup; mutant n6 (table renamed before its fsync) killed: job 261113-042432-0f33ed |
| A3 | job 261113-044510-384de8 (`tests/detent_agent/agent_store_sole_writer.sh`) and the suite, job 261113-044509-c22720 |
| A4 | job 261113-044511-bfb064 (test_agent_store: `collection`, `held_lease`); mutants a4-collect-ignores-lease and a4-publish-ignores-lease killed: jobs 261113-042414-b3c4fb, 261113-042418-606cb1 |
| A5 | job 261113-044509-c22720 (test_agent_catch_up: a kill at each of 33 primitives, 29 recovered at R and caught up by ring, block and base) |
| A6 | [artifact] `docs/status/namespace-s7.md`: "## Wiring" |

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
- A paged publication is reserved before its plan moves (a refusal leaves the plan whole); a flat `.live`
  is charged to the store's account (pressure refuses before queueing).
- `change` grows a slot with the namespace's first publication (AgentStore's rule for any entry); the
  change file test follows it rather than AgentStore reserving slot 0 at creation.
- The reader's first open counts the table and the image peek as record opens and the table's digest
  as bytes hashed (read_record already counts their bytes); job 261113-044512-76bfe6 runs CI's
  counters configuration.
- Collection skips a generation whose `.lease` a view still holds; a dropped view closes it on the
  retirement thread, so a superseded base may wait for the next publication's collection.
- The prepared reader fixture writes no `owner.lock` (its callers do, as before).
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
