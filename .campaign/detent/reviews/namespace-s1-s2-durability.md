# namespace-s1-s2 review: durability

Commit: b3fef5099a0512c31b1ccee1b56e45ccb8b9daf7
Verdict: CHANGES
Date: 2026-11-08

APPROVE: lands as is, at this commit's code. CHANGES: lands after each blocking finding is fixed
and confirmed by a job at the fixed commit. REJECT: the approach is wrong; the quest is re-planned.
An APPROVE lists no blocking finding; a CHANGES lists at least one.

## Evidence I relied on

None for A4, A5, A7 or A8: at this commit no job establishes them. `campaign status namespace-s1-s2`
shows 0 of 13 criteria with evidence, and the journal's Evidence table is empty. The jobs that
exist for these criteria all failed to build or were skipped (see D2):

- 261108-212825-a55bba (positive power-cut and startup tests): failed, exit 125, `ninja: error: unknown target 'test_local_publication_power_cut'`.
- 261108-213111-929a69 (unsynced-descriptor), -213112-ecd616 (empty-payload-group), -213114-e47da1 (startup-before-barrier), -213115-984fa5 (closed-session), -213116-edd4b1 (phase-counts): skipped, exit -1.
- Gates native, asan and tsan at `Detent:c2025c2` (261108-212143-1824ae, -df3187, -69e836): cancelled, exit 143. Pins (261108-212143-7ae4f5) passed; it is not my lens.

What follows is from reading the code at this commit and at `completion:560c288`.

## What I checked myself

Scripts are under `.scratch/review-namespace-s1-s2-durability/` in the home checkout.

- `probe.sh census-meta` with `probe_census_meta.cc`. It builds a Debug test target and creates a
  fresh store. Just before the first sync that follows a write, it copies the directory: the
  process-kill view, every written byte present and nothing synced. It then reopens that copy as
  ExistingManaged, with completion's sync seam and request-level tracking. It exits 1 if the store
  reports ready while storage.meta was never fsynced (on macOS, also flushed). It exits 0 if
  storage.meta was durable at ready, never appeared under its final name unsynced, or the store
  refused to start. Job 261108-214023-7aabf1 failed with 125 on the same configure defect as D2.
  The corrected probe, job 261108-215124-d07d1a, was still queued behind a strict hold when I
  wrote this. Expected at b3fef50: exit 1. Expected at a fix: exit 0. D1 rests on the code
  references below. The probe is how the lead confirms the fix.
- Risk 1, crash windows: `PublicationOperation` (disk_store.cc:455-772). Group A is the owners and
  the lease (paged_publication_artifacts.cc:66-79, `first_group_`; non-paged `group_end_ = 1`).
  Group B is the packs, image, live, read and descriptor. Each window of at most
  `sync_group_files` files is one `SyncFiles`. On Linux that is fsync of each file, then of the
  directory; on macOS, fsync of each, fsync of the directory, then one `F_FULLFSYNC` per device
  (completion posix.h:606-614, 187-232). Group B starts only after group A's last window has
  synced and closed. `Replace` is issued only after the final window, the one holding the
  descriptor, returns. `selection` and `change->publish` change only after the post-rename
  `DirectorySync`. Restart: a kill between Replace and its barrier leaves a visible but non-durable
  rename whose files are already durable. The startup `DirectorySync` (disk_store.cc:372) makes the
  rename durable before `ready()` bumps `change`. I found no path where `selected` names a
  descriptor whose owners, packs or lease were not synced first. Reused segments come only from
  the verified selected generation.
- Risk 2, windows: cancel is deferred while `window_open_` (set at the window's first submission,
  cleared after its last close) and acted on in CloseGroup, before the next window or Replace. An
  error anywhere goes through `fail()`, which closes the window's open files and fences, with no
  rename. Nothing is renamed, `selection` is not set and `change` is not bumped before every
  window has returned. Correct by reading. Error injection over a multi-window paged publication
  is not tested (N2).
- Risk 3, census: `ready()` is reachable only after the startup `DirectorySync` (disk_store.cc:171)
  or after the storage.meta preparation, which is CloseAfterSeal: Sync and DirectorySync
  (disk_store.cc:130). The store sets `selection` only in `ready()`. Every publication file the
  census trusts was synced before `selected` could name it. storage.meta is the exception (D1).
- Risk 4, platforms: the counters (storage_session.cc `sync_performed`) match completion's call
  sequences. macOS: Sync, DirectorySync and each SyncFiles cost one flush. Linux: one per fsync,
  an assumption by contract, labelled as such in sync_counters.hh. T7's model is sound for
  `SyncFiles`. On Linux, fsync makes data durable and a directory fsync makes the entries durable.
  On macOS, fsync only stages and `F_FULLFSYNC` flushes what was staged. It is conservative in not
  crediting a file fsync with its entry. The power-cut views are Truncate and Zeros for data, and
  for entries either the durable ones or all the volatile ones. It also treats `F_BARRIERFSYNC` as
  a full flush (N3).
- Risk 5, mutants: by reading, each mutant in `mutant.sh` targets its claim. unsynced-descriptor
  is A4's "rename before fsync". empty-payload-group and startup-before-barrier are A7's.
  closed-session and phase-counts are the A8 reverts of `Detent:0cd1527` and `Detent:522452a`. No
  job has run any of them (D2).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| D1 | yes | defect | The census trusts storage.meta without syncing it, and storage.meta is created under its final name before its own Sync. Sequence: a kill after the storage.meta Write and before its Sync (CreateInto, Write, Sync, DirectorySync, completion image.h:92-95, 340-345), a restart as ExistingManaged (the relay's default; generation_writer.cc:123 picks it whenever storage.meta exists), then a power cut. The restart reads and decodes it (disk_store.cc:262-265, 327), issues only the directory barrier (:372) and reports ready. Publications then embed that identity in leases and owner records. After the cut, storage.meta can be zero-length or missing (ext4 delalloc; on macOS the UBC is not flushed for other files). The next start fails with "unsafe managed storage record" or "managed storage identity missing", while durable generations still carry the lost identity. main closed this window: its census synced every record it read (main disk_store.cc:138; main test_paged_store_startup.cc:79, `syncs == 6`, storage.meta among them). D11 says "restart syncs no file" only for files the table reaches; storage.meta is not one of them. T7 never exercises store creation: its seed is fully durable (test_publication_power_cut.cc:404). | Code references; probe job 261108-215124-d07d1a (queued; 261108-214023-7aabf1 hit the D2 configure defect) | Options that keep one startup barrier: (a) make the startup barrier one `SyncFiles` over storage.meta (opened read-only) plus the directory: on macOS one flush as now, on Linux one extra fsync; (b) create storage.meta synced before it is named (a temporary name plus rename, which decision 2 defers to S7); (c) sync storage.meta only on a start that did not create it but finds it, which narrows the cost to the one record. Add a T7 or startup case that cuts store creation. Rerun `probe.sh census-meta`. |
| D2 | yes | gap | A4, A5, A7 and A8 have no evidence, and the mutant harness cannot produce any. `mutant.sh` (docs/measurements/sync-groups-20261108/mutant.sh:15-17) configures AUTHORITY, RELAY and MANAGED_AGENT all OFF. completion is then never fetched (CMakeLists.txt:94-99), `detent::local_publication` is undefined (src/detent_local/CMakeLists.txt:20-21), and the test targets do not exist: the positive job exits 125. A "must fail" mutant job that exits 125 also reads as failed, so a killed mutant and a broken build look the same. All five mutant jobs were skipped. Also missing: A7's "per-file-order revert", and any T7 run after the merge. | 261108-212825-a55bba log: `unknown target 'test_local_publication_power_cut'`; mutant jobs skipped | Configure with `-DDETENT_BUILD_MANAGED_AGENT=ON` (as `probe.sh` now does). Have the job pass only when the mutant's test fails with a nonzero exit other than 125 (or 125 maps to an error the job treats as "not evidence"). Run the positive, the five mutants and the per-file-order revert, and record each test's rc. For startup-before-barrier, show that T7, not only the barrier count in `test_local_paged_store_startup`, kills it. |
| N1 | no | gap | The restart-then-republish cycles sample every third event (`j += 3`, test_publication_power_cut.cc:497), and the readiness check at `i == run.ready` runs only on the first pass, from a fully durable seed, where it holds trivially. The startup-before-barrier mutant is caught in T7 only if a sampled cycle event lies between ready and the rerun's first `SyncFiles`. | test_publication_power_cut.cc:404, 425-430, 497 | Check durability at ready in each cycle's rerun, or sample every boundary event in cycles. |
| N2 | no | gap | No fault injection over a multi-window paged publication. publication_faults (test_disk_store.cc:741+) covers the three-file non-paged path, which fits in one window, so `fail()`'s close-the-window path with a nonzero `close_index_` (and Abort over many files) is proven only by reading. | test_disk_store.cc:741-775 | Inject at each primitive of a paged publication with `sync_group_files` 2. |
| N3 | no | nit | T7's macOS seam treats `F_BARRIERFSYNC` as a device flush (test_publication_power_cut.cc:81-86). Publication never issues one today, but a future switch to `OrderFiles` (the consumer pattern PLATFORM_FINDINGS.md:114 recommends) would pass the model on macOS without the guarantee on non-Apple drives. | test_publication_power_cut.cc:82 | Model a barrier as ordering only, or reject it in the seam. |
| N4 | no | nit | A kill between Replace and its directory barrier lets readers on this host serve the new generation before its rename is durable; after a power cut they see the older one. This behavior is unchanged from main and is not introduced here. | disk_store.cc Replace/Barrier phases | None needed for this quest. |

## Not run

- Probe 261108-215124-d07d1a had not run when this was written. D1 rests on the code references;
  the lead confirms by running the probe at b3fef50 (expected exit 1) and at the fix (expected
  exit 0).
- No T7, mutant, sanitizer or ctest run succeeded at this commit, mine or the contributor's.
- Linux and FreeBSD behavior: by reading only; no Linux job.
- The legacy upgrade path (UpgradeLegacy census, which no longer syncs the legacy selected, image
  and lease): by reading only. It is safe if the legacy writer synced before renaming, which
  main's CloseAfterSeal and Retain preparations do.
- Reclaim and GC after a cut that leaves group A without group B (owners and lease durable, no
  packs): not checked.
- A6 counters exposure (agent log, relay JSON), A9 timings, A10-A13: outside this lens.
