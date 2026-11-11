> Status: DRAFT (2026-11-04) How a cold start trusts a prepared generation, checks pages on first touch, scrubs packs and repairs a damaged generation. Workstreams: namespace-s3, namespace-s3b. Design: [namespace-delivery.md](../design/namespace-delivery.md) D10. Written from the code on `namespace/s3b`; not yet reviewed.

# Trusted restart, first touch and the scrub

A generation is verified once, by the build that writes it. A cold start (no prior generation, no admission) over a generation that has a live record maps its packs without reading or hashing them. Every later read of a page from such a pack checks the page against its parent's reference the first time. A paced scrub hashes whole pack files against their owner records, and a damaged generation is replaced by a repair publication. Code: `src/detent_local/trusted_restart.cc`, `live_record.cc`, `verified_pages.hh`, `paged_navigation.hh`, `pack_scrub.cc`; wired in `generation.cc` and `paged_publication_artifacts.cc`.

```text
build (publication)                          cold start (RecoveryEngine::recover)
  segments, .owner, .pack                      selected -> lease -> storage.meta -> .image   (root: 320 B hashed)
  .lease .image .live .read .descriptor        .live present?  no  -> full verified recovery (walk, 3 hashes/pack)
  all synced, then `selected` replaced                         yes -> TrustedRestart::open
                                                  .read  (digest, bindings)   .live (digest, bindings)
                                                  per row: open .pack, mmap, no read, first-touch bits
                                                  adopt rows -> sealed registry; counts from .read
                                               per segment: .owner record == row; named file == mapping (existing loop)
```

## The live record (`generation-<id>.live`)

What the recovery walk derives from the pages and a restart cannot recompute without reading them: each segment's live page and byte counts (they pick which segments a successor carries and which to compact), the segment list itself, and the override histogram. One record per generation, written by the publication that wrote the generation, from the plan's own reachability (`PagedPublicationArtifacts::populate`).

Fields, big-endian, `live_record.hh`:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 8 | magic `DTLIVE01` |
| 8 | 4 | version, 1 |
| 12 | 4 | total length, equal to the file size |
| 16 | 16 | reserved, zero; the decoder refuses anything else (the record holds nothing host-specific, so two builds of one input write the same bytes) |
| 32 | 16 | generation id |
| 48 | 32 | root checksum, equal to the descriptor's and the lease's image checksum |
| 80 | 32 | the `.read` file's own trailing SHA-256 (bytes 160-191) |
| 112 | 4 | row count n, equal to the root's `segments` |
| 116 | 4 | zero |
| 120 | 1028 | override histogram, 257 words |
| 1148 | 4 | zero padding to 1152 |
| 1152 + 72 i | 16 / 8 / 32 / 8 / 8 | row i: segment id, length, pack SHA-256, live pages, live bytes |
| 1152 + 72 n | 32 | SHA-256 over every preceding byte |

Rows are strictly ascending by segment id, so they are the registry's order. A row needs a nonzero id, a length of at most one segment, at least one live page and no more live bytes than its length. The decoder refuses any other shape (`decode_live_record`).

**Binding.** The trailing digest covers the file. The header binds it to the generation (id and root checksum) and to the exact `.read` file by that file's own digest, so a record or `.read` copied from another generation or another build of the same generation is refused; `.read` carries the storage identity and is checked against `storage.meta`. Three further checks run at the open, none needing I/O:

1. The sum of the row lengths plus the root equals `.read`'s physical bytes, and the row count equals the root's `segments`.
2. The derived counts add up to what the root and `.read` authenticate: the rows' live pages sum to `root.pages` and their live bytes to `root.bytes`; the histogram sums to `root.records`, its weighted sum equals `.read`'s override count and its highest nonzero bucket equals `.read`'s maximum overrides.
3. The segment holding the tree's first page (`root.tree.segment`) is one of the rows.

The per-segment loop at the end of `recover` then reads each `.owner` record and requires it to equal the row's ownership (storage, id, length, checksum), and stats the named pack for owner, mode, link count, length and the identity of the mapped inode. That proves each row names a real, owned pack of the right length; it does not prove the rows are the set the root reaches. A digest-valid record with another generation's rows is caught by check 3 when the tree's segment changed; when only other segments differ, a read that reaches one of them finds no segment and reports `SourceFailure` (`PagedReadState::lookup`), never an exception. Binding the row set itself needs a manifest digest the root or the table authenticates; that is a format decision the design leaves open.

**What the counts drive, and what is not caught.** The live pages and bytes decide which base segments a successor registry carries (`WarmRegistryMerge`) and which packs compaction picks; the histogram sets the successor's maximum overrides, which readers enforce. The sums above are the totals a builder defect or a damaged record can be caught on for free. A pair of rows whose pages were shifted between segments with the sums kept passes every check at the restart and is refused by the successor's warm validation at the next update (`invalid warm paged graph`), which then fails every update until the store is replaced. No read returns a wrong value on that path.

**Order against the table.** `.live` is one more artifact of the generation, appended between `.image` and `.read` and written, synced and renamed with the others before `selected` is replaced, so no durable `selected` reaches a generation without it. The collector removes it with the generation's other files (part 4 of its pass, inspected and rechecked like the rest, removed before the lease).

**Missing or damaged.**

- Missing, or present and wrong (digest, binding, shape, size, an unsafe file, a missing `.read`, `.read` differing from the root or limits): the store is refused (`recover` throws). There is no second restart path; a refused store is repaired the way a corrupt one is, by a fresh copy (lead ruling 2026-11-04). The test suite's hand-installed stores derive both records once (`tests/detent_local/trusted_records.hh`).
- A corrupt `selected`, root image or lease refuses the store exactly as before: those are checked first.
- The refusal applies to the agent's cold start (`RecoveryContext::trusted_start`, set by `ManagedRecovery::capture`). The full verified recovery stays for the recovery that follows a publication and for tools (`recover_generation`).

## Fuzzing

`tests/detent_testing/live_record_fuzz.cc` is a libFuzzer driver for `decode_live_record` (`build_live_fuzz.sh` builds it with ASan and UBSan). It decodes each input raw, then mutates a valid record and repairs the trailing digest so the structural checks behind it run, and requires that a record that decodes encodes back to the same bytes. A 181 s run executed 1,185,596 inputs with no finding.

## What startup does and does not do

Reads and hashes: `selected`, the lease, `storage.meta`, the 320-byte root (hashed against the descriptor), `.read` (its own digest), `.live` (its own digest), and per segment an `.owner` record and a stat. Per segment it opens the pack, checks owner, mode, link count and length, and `mmap`s it; it reads none of the pack. A counter proves it: `RecoveryMetrics::segment_hash_bytes` is 0, `root_hash_bytes` is 320, and `paged::hash_counts().page_calls` (a process-wide count of page digests) does not move (`test_testing_trusted_restart`).

## First touch

`SegmentMapping::require_first_touch` gives a trusted mapping a `VerifiedPages` bitmap, a four-bit start code per 80-byte granule (the reader's bitmap, `lazy_page_access.cc`, now uses the same class). The registry copies the bitmap's address into each `SegmentInput`; a mapping the recovery walk compared has none. Every read of a page by the agent goes through `checked_page_bytes` (`paged_navigation.hh`) or, for lookups, `PagedReadState::lookup`, which calls it: when the input has a bitmap and the page's start is unmarked, `paged::verify_page` (the page's header, digest and structure against the parent's reference, the same check the reader runs) runs once and marks it; a mismatch throws `ReadSourceFailure`, which a `ReadView` read reports as `SourceFailure`. The mark lives on the mapping, so a successor registry that carries the mapping (`append`) carries the verified state. The read sites are `paged_trie_overlay` (delta and compaction plans), `paged_contextual_reuse`, `paged_cursor`, `paged_warm_validator`, `paged_dependency_walk`, `paged_graph_validator` and the lookup above.

A successor's warm validation reads only changed pages of its base, through the same accessor; pages it never reads it never checks, and those are the scrub's.

## The scrub

`PackScrub` (`pack_scrub.hh`) takes the registry's inputs (owner record checksum, length and verified-page bits per pack) and the store directory, and hashes the pack files in order at `bytes_per_second`: a credit of `rate * elapsed` bytes accrues (capped at one second, so a stalled caller is not repaid in a burst), the hash advances in slices up to the credit, and a pack whose SHA-256 differs from its owner record is listed in `failures()`. A pass over B bytes takes B / rate seconds. The caller picks the clock and the cadence; the class starts no thread. `test_testing_pack_scrub` checks the pace against a fake clock exactly and against the wall clock within 10%.

**It reads the file, not the mapping.** Each pack is opened through the directory (`openat`, `O_NOFOLLOW`, `O_NONBLOCK`), its length is compared with the owner record's, and it is read with `pread` in slices: `F_NOCACHE` on macOS keeps the pass out of the unified cache, and `posix_fadvise(POSIX_FADV_DONTNEED)` after each slice does the same on Linux. The agent's mapping is `MAP_PRIVATE`, so on macOS it never shows a later write, and neither platform re-reads media for pages the mapping keeps resident; the file read sees what the kernel serves for the file now. A pack that is missing, a directory, resized or unreadable is reported like one whose hash differs. `test_testing_trusted_restart scrub-disk` rewrites 40 bytes after the packs were mapped (the security reviewer's probe) and the scrub reports exactly those packs.

**A report withdraws the verified pages.** A pack the scrub reports clears its `VerifiedPages` bits (`SegmentInput::pages`), so the next read of each page checks it again against its parent reference and fails with `SourceFailure` if the page is damaged; the packs nobody damaged keep their bits. A first touch racing the clear can leave its own page set; the next pass reports the pack again.

The scrub reports at pack granularity.

**In the agent.** `Coordinator::Impl::scrub_loop` is a third sibling task beside the disk and candidate loops (`task::when_all`). Every 20 ms it pins the selected generation's registry (`ScrubSource`, a retained handle) and posts one slice of at most 2 MiB to the configuration worker. Opening, reading and hashing run on that worker. The owner waits asynchronously for authentic completion before reading the result, changing the pinned root or closing. The loop reads `base->generation` only after the recovery worker has set `ready`. A pack that differs from its owner record sets `CoordinatorStatus::scrub` (`passes`, `bytes_hashed`, `damaged_packs`, `source_failure`), counts one coordinator failure with a scrub error, requests a repair (below), and the agent's monitor logs it as a warning. `scrub_bytes_per_second` (managed configuration, default 524,288, 0 disables) sets the pace.

A failed pack open or slice read is retried once at the same offset. A directory open is also retried once; persistent failure ends the pass with a coordinator error and no damaged-pack report. It cannot attribute the failure to a pack. The next tick retries the pass.

Each worker slice retains its scrub state and leased root until the worker sets `ready` with release ordering. The owner waits with acquire ordering before inspecting or replacing them. Shutdown waits for that same completion; it cannot abandon an accepted slice. A result whose root differs from the store's current selection is discarded, so an old generation's damage cannot request repair of its replacement. `test_delivery_repair_scrub-worker` pauses a completed pass while a second worker publishes a repair, and pauses another pass across shutdown.

## Repair of a damaged pack

A store has one generation today, so "a namespace fails alone" means the coordinator keeps running and readers keep serving what they can read; a damaged page fails the reads that reach it with `SourceFailure` and nothing else. Code: `coordinator.cc` (`request_repair`, the candidate loop, `bind_base`), `paged_snapshot_plan.cc`, `paged_plan.cc`, `disk_store.cc` (`begin_paged`, `capture`), `paged_segment_collection.cc`, `bundle.cc`.

```text
scrub report ----+                                       +--> Replace at the selected cursor or later
first-touch      +--> request_repair --> pending ------> | force_snapshot, session reopened (poll: no base)
failure (update) +                                       +--> PagedSnapshotPlanBuilder(repair): intent Repair
                                                              begin_paged --> fresh packs, selected replaced
   recovery: no prior (admission and capture) --> full walk of the new generation
   bind_base: pending off, completed + 1, request_collection --> collector removes the damaged packs
```

**Trigger.** `Coordinator::Impl::request_repair` sets `diagnostic.repair.pending` (`CoordinatorStatus::repair`: `requested`, `completed`, `pending`). Two things call it: the scrub loop when a completed pass found a damaged pack (it also ends a network wait in progress, `repair_woke`, which is not counted as a failure), and the candidate loop when building an update throws `ReadSourceFailure` (a first-touch failure in a delta, progress or compaction plan). While a repair is pending the round skips compaction, sets `force_snapshot`, closes a session that was not opened for a Replace and opens the next one with `force_snapshot`, and in poll mode sends no base. The state is the coordinator's, not durable: a restart during a pending repair finds the damage again at the first scrub pass.

**Build.** The upstream sends a snapshot. When its cursor is the selected one the repair is in place: the relay sink's staging is skipped, because the cache already holds the revision. `build_paged` passes `repair` to `PagedSnapshotPlanBuilder`, which picks intent `Repair` when the target has the base's identity, in place of `Maintenance` (the equality check against the base, which would throw on a damaged page or find nothing changed and publish nothing). A `Repair` plan needs the base's identity and profile and a revision at or after the base's (`PagedPlanState`); `begin_paged` accepts it at an unchanged cursor. A snapshot shares no pack with its base, so the generation is built from the incoming snapshot alone and reads nothing of the damaged one; `test_testing_trusted_restart heal` checks that every pack of the repaired generation is new.

**Recovery without the prior.** Every snapshot plan owns a replacement image and shares no pack with its base. `MappedRecoveryAdmission::populate` and `ManagedRecovery::capture` give it no prior, so the engine walks and hashes the new generation without reading the base. Deltas and sparse compactions validate warm against the prior (`core.verified_paged`). `bind_base` confirms a repair, clears `pending`, counts `completed`, calls `PublicationSink::selected_repaired` (the relay `Bundle` swaps its head and keeps its cache) when the repair is in place, and asks the store for a collection. Any confirmed snapshot also clears a pending repair, including a non-`Repair` identity reset; only a `Repair` increments `completed`. A delta or progress publication keeps every pack it does not rewrite, so it leaves `pending` set, and the next round reopens the session for a Replace. A session opened with `force_snapshot` counts as forced only until its first offer is published, so a repair requested later reopens it too. A refused backwards replacement keeps `pending` and records the failure with reconnect backoff.

A repair at the selected cursor accepts a state that differs from the selected generation. It cannot use damaged base pages for a state-equality comparison. It still requires the selected base's identity and profile and refuses an older revision.

**Collection.** `PagedSegmentRead` takes `tolerate_mismatch` for unmarked sweep candidates: the collector proves the owner record and the file's ownership, mode, link count, length and identity, then removes the pack even if its payload checksum differs. The dependency walk refuses a mismatching pack. If a protected root's walk fails proof, the collector reads its `.live` record through completion I/O, checks the record's digest, generation, root checksum, row count and tree membership, and conservatively marks every row. It retains the proof failure in `DiskStore::collection_error()` while sweeping other unreachable packs. Invalid or unavailable metadata stops the sweep. Conservative rows never enter the dependency-certificate cache.

**Deferred collection.** A pass that retains a non-selected root, or conservatively marks a failed root, leaves collection pending. A cached certificate can protect a damaged root without reading its packs, so retry depends on retained roots as well as proof failures. `DiskStore::advance` schedules one retry after a completed deferred pass; the coordinator includes `collection_retry_at()` in its existing disk deadline. The delay doubles from one second to a 30-second cap. File leases have no release notification, so the cap bounds the post-release wait while backoff limits repeated scans during a long lease. A successful pass clears the deadline and resets the delay. Close hides the deadline and admits no retry. No timer or thread is added. `test_delivery_repair_heal` retains every damaged extent through two passes, checks backoff, and releases the reader without requesting collection again; `lease-close` closes with deferred work.

**What a reader sees.** The agent never fences for this. A reader process maps `selected` itself: before the repair it serves what it can read and gets `SourceFailure` for the damaged pages; after the repair its next open maps the new generation. `test_delivery_repair heal` runs a reader that mapped the damaged generation through the whole repair: no read returned a wrong value and none that worked stopped working.

**Limits.**

- A page that changes after verification can return incorrect values until the scrub withdraws its verified bit. Every lookup walks the page with `paged::NodeAt`, which bounds-checks each field it decodes, and maps any decoding failure to `ReadSourceFailure`; `try_get` and `try_eval` return `SourceFailure` for decoding or evaluation exceptions. Allocation failures and storage admission pressure still propagate.
- The relay's snapshot stage reads the head; if the head is damaged that stage fails first touch and is not yet a repair trigger.
- S7 adds ordinal r + 1 to the generation id; the coordinator state above stays.

## Limits

- Corruption nobody reads stays undetected until the scrub reaches it (design Limit 1). A page verified before it was damaged stays verified for the mapping's life.
- Padding between pages is covered only by the scrub (no page digest includes it).
- Trust ends at the live counts (above) and at `.owner` records, which a writer of the store can forge as it can forge any file there.
- Only a cold start with no admission trusts the prepared records. Recovery after any snapshot walks the new generation without its prior. Recovery after a delta or sparse compaction validates warm, carrying the prior's mappings and their first-touch bits.

## Design vs code

See `docs/status/namespace-s3.md`.
