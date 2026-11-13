# namespace-s7-store: journal

State: native suite 261113-064836-7523c4, pins 261113-064836-2db804 and F1 confirm 261113-064848-247cb4 green at Detent:3591767a; ready for review once two out-of-set CI fixes land (Blocked on)
Quest: .campaign/detent/quests/namespace-s7-store.md (the lead's; never edit it)

## Doing

msg-47/msg-48 closed by the lead (file set widened). Ported at Detent:a712310d: the four delivery tests
(trusted_records installs; `migrate` modes are `from-flat` over an installed flat store, D43),
test_managed_process.sh (a store mark: `change`'s inode and the table sequence, never going back) and
verify_local_format.py (reads the table), the three benchmarks, check_lineage.sh, first_read.py, docs.
Durability review (CHANGES at 518f6147) folded in at Detent:f6d59fae: F1 fixed, F2 and F3 tested, F6
done, F7 ported.

## Next

1. Read the jobs; on green set "ready for review" (after the two out-of-set fixes land in CI).
2. Once the set covers them: drop `paged_snapshot_bench.cc`'s dead StorageIdentity and USAGE.md's
   `--upgrade-managed` sentence, rerun the benches job and push CI through the lead.
3. A test_relay_chain timeout in the native suite is reported, not chased (lead, F5).

## Blocked on

CI (run 43435676235 linux-benchmarks, Docs run 43435676004) fails on two files outside the set, both
left by this quest's deletions (D43):
1. `benchmarks/paged_snapshot_bench.cc:41-42` declares an unused `l::StorageIdentity storage` (ownership.hh
   is deleted). Fix: delete the two lines.
2. `docs/USAGE.md:222` names `--upgrade-managed` (deleted with UpgradeLegacy); check_docs.py reports
   `docs-only flag --upgrade-managed`. Fix: drop that sentence.
Recommendation: add both files to the set; each is a deletion.
Follow-up for the lead (src/detent_relay, outside the set): the relay's native queue, driver and
CompletionScheduler no longer carry disk work (the agent store runs on the coordinator's worker), so its
`disk_*` status fields read 0 and `CoordinatorOptions::completion_port` is only validated.

## Review findings (durability, 518f6147)

| Finding | What changed | Test, and the job that kills its mutant |
|---|---|---|
| F1 | `collect()` keeps a generation whose `.lease` fails to open for any reason but ENOENT (and the segments its `.live` names, or all when `.live` does not read); `publish()` throws LeaseHeld | `test_agent_store` `lease_open_failure` (EMFILE, EACCES via an interposed `openat`): job 261113-052759-b162ba; mutants s7-lease-collect-fail-open 261113-052806-da03ca, s7-lease-publish-fail-open 261113-052809-87f71f; F1 confirm 261113-064848-247cb4; final suite 261113-064836-7523c4 |
| F2 | The catch-up child confirms the recovery (which drops the plan's view of the base), waits for the base's lease, then requests the collection, so cuts land in it | `test_agent_catch_up`, suite 261113-064836-7523c4 |
| F3 | none (test only) | `test_local_disk_store` live-charge case; mutant s7-flat-live-private-ledger 261113-052823-734a00 |
| F5 | not chased (lead); did not recur in 261113-052843-16886a | native suite 261113-064836-7523c4 |
| F6 | `collection_retry_at()` back on DiskStore (a collection that leaves a generation the table does not reach is deferred) and in the coordinator's `wait_disk` | `test_delivery_repair_heal` / `lease-close` (deferred passes, backoff, release without a request), suite |
| F7 | verify_local_format.py, test_managed_process.sh, benches ported | suite; benches 261113-052745-a0b1a8 |
| F4, F8 | not done (optional nit; outside the lens) | |

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | job 261113-064836-7523c4: the native check, no exclusions (test_system_managed_*, test_managed_*_process, test_delivery_coordinator*) |
| A2 | job 261113-052802-4165a0: T7; mutant n6 killed: job 261113-052820-12513f |
| A3 | job 261113-052825-2d2540 (`agent_store_sole_writer.sh`) and the suite, 261113-064836-7523c4 |
| A4 | job 261113-064848-247cb4 at Detent:3591767a (earlier 261113-052759-b162ba; test_agent_store: `collection`, `held_lease`, `lease_open_failure`); mutants killed: a4-collect-ignores-lease 261113-052813-b190fd, a4-publish-ignores-lease 261113-052816-abaf97, s7-lease-collect-fail-open 261113-052806-da03ca, s7-lease-publish-fail-open 261113-052809-87f71f |
| A5 | job 261113-064836-7523c4 (test_agent_catch_up: a kill at every primitive, collection included) |
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
  retirement thread. A collection that leaves such a generation reruns after 1 s doubling to 30 s
  (`collection_retry_at`, as main's DiskStore did), so a released base goes without a new publication.
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
- The `migrate` coordinator modes become `from-flat`: the store holds an installed flat generation and the
  paged publications replace it (no UpgradeLegacy, D43). The failed-startup case fences on a table naming a
  missing generation; the reader-pressure limit is 4096 bytes (two flat generations of 1376 bytes fit, a
  third's 1864-byte reservation does not while a reader pins the first).
- The coordinator fences instead of terminating when its scrub state cannot be admitted (storage pressure at
  startup): `test_delivery_coordinator`'s pressure case aborted on it once the store no longer freed startup
  objects into that account.
- test_managed_process.sh proves the store continues (it compared `storage.meta`): the `change` file's inode
  is unchanged and the table's sequence never goes back across restarts and the trusted reset.
- The collection benchmark measures `AgentStore::collect` through GenerationWriter (a repair supersedes a
  generation sharing no pack, then an idle pass); PagedSegmentCollection is gone.
- DISK_LIFECYCLE_ARCHITECTURE.md and DISK_STORE_INTEGRATION.md now describe the agent store and DiskStore
  in a page each; the old blueprints describe deleted code. diagrams/11_how_store.png still draws
  `storage.meta` (diagrams/ is outside the file set; the caption no longer names it).
- Suite 261113-052843-16886a failed on two port errors, fixed at Detent:3591767a: test_agent_catch_up
  waited for the base's lease before confirm, but the pending recovery's plan views the base until
  confirmed (the wait could never end); test_managed_process.sh asserted relay disk work on the native
  completion queue, a path the agent store replaced with the coordinator's worker (Wiring), so it now
  checks the ready relay's table and image. Locally all five pass (and test_local_disk_store).
- `disk_store.hh` includes `<exception>` (macOS libc++ CI failed on `std::exception_ptr`).
