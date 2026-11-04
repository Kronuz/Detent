> Status: DRAFT (2026-11-03) How a cold start trusts a prepared generation, checks pages on first touch and scrubs packs. Workstream: namespace-s3. Design: [namespace-delivery.md](../design/namespace-delivery.md) D10. Written from the code on `namespace/s3`; not yet reviewed.

# Trusted restart, first touch and the scrub

A generation is verified once, by the build that writes it. A cold start (no prior generation, no admission) over a generation that has a live record maps its packs without reading or hashing them. Every later read of a page from such a pack checks the page against its parent's reference the first time. A paced scrub hashes whole packs against their owner records. Code: `src/detent_local/trusted_restart.cc`, `live_record.cc`, `verified_pages.hh`, `paged_navigation.hh`, `pack_scrub.cc`; wired in `generation.cc` and `paged_publication_artifacts.cc`.

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
| 16 | 16 | storage identity |
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

**Binding.** The trailing digest covers the file. The header binds it to the generation (id, storage, root checksum) and to the exact `.read` file by that file's own digest, so a record or `.read` copied from another generation or another build of the same generation is refused. The rows are checked a second time against ground truth that is already read at every start: the per-segment loop at the end of `recover` reads each `.owner` record and requires it to equal the row's ownership (storage, id, length, checksum), and stats the named pack for owner, mode, link count, length and the identity of the mapped inode. The sum of row lengths plus the root must equal `.read`'s physical bytes. What the digest cannot prove is the live counts: a wrong pair changes compaction choices, not values.

**Order against the table.** `.live` is one more artifact of the generation, appended between `.image` and `.read` and written, synced and renamed with the others before `selected` is replaced, so no durable `selected` reaches a generation without it. The collector removes it with the generation's other files (part 4 of its pass, inspected and rechecked like the rest, removed before the lease).

**Missing or damaged.**

- Present and wrong (digest, binding, shape, size, an unsafe file, a missing `.read`, `.read` differing from the root or limits): the store is refused (`recover` throws).
- Absent: the full verified recovery runs, as for any generation published before `.live` existed. This is the one place the behavior differs from "refuse"; it keeps an upgraded store working at its old restart cost, and an attacker who deletes the file gains nothing (every digest is then checked). Whether absence should refuse is the owner's call (ledger, "Design vs code").
- A corrupt `selected`, root image or lease refuses the store exactly as before: those are checked first.

## What startup does and does not do

Reads and hashes: `selected`, the lease, `storage.meta`, the 320-byte root (hashed against the descriptor), `.read` (its own digest), `.live` (its own digest), and per segment an `.owner` record and a stat. Per segment it opens the pack, checks owner, mode, link count and length, and `mmap`s it; it reads none of the pack. A counter proves it: `RecoveryMetrics::segment_hash_bytes` is 0, `root_hash_bytes` is 320, and `paged::page_digests()` (a process-wide count of page physical and logical digests) does not move (`test_testing_trusted_restart`).

## First touch

`SegmentMapping::require_first_touch` gives a trusted mapping a `VerifiedPages` bitmap, one bit per 80-byte granule (the reader's bitmap, `lazy_page_access.cc`, now uses the same class). The registry copies the bitmap's address into each `SegmentInput`; a mapping the recovery walk compared has none. Every read of a page by the agent goes through `checked_page_bytes` (`paged_navigation.hh`) or, for lookups, `PagedReadState::lookup`, which calls it: when the input has a bitmap and the page's bit is clear, `paged::verify_page` (the page's physical and logical digests against the parent's reference, the same check the reader runs) runs once and sets the bit; a mismatch throws `ReadSourceFailure`, which a `ReadView` read reports as `SourceFailure`. The bit lives on the mapping, so a successor registry that carries the mapping (`append`) carries the verified state. The read sites are `paged_trie_overlay` (delta and compaction plans), `paged_contextual_reuse`, `paged_cursor`, `paged_warm_validator`, `paged_dependency_walk`, `paged_graph_validator` and the lookup above.

A successor's warm validation reads only changed pages of its base, through the same accessor; pages it never reads it never checks, and those are the scrub's.

## The scrub

`PackScrub` (`pack_scrub.hh`) takes the registry's inputs (owner record checksum and mapped bytes per pack) and hashes them in order at `bytes_per_second`: a credit of `rate * elapsed` bytes accrues (capped at one second, so a stalled caller is not repaid in a burst), the hash advances in slices up to the credit, and a pack whose SHA-256 differs from its owner record is listed in `failures()`. A pass over B bytes takes B / rate seconds. The caller picks the clock and the cadence; the class starts no thread. `test_testing_pack_scrub` checks the pace against a fake clock exactly and against the wall clock within 10%.

The scrub reports at pack granularity. It is not yet driven by the agent: nothing calls `begin` on a timer or exposes `failures()` in health (ledger, "Next").

## Limits

- Corruption nobody reads stays undetected until the scrub reaches it (design Limit 1). A page verified before it was damaged stays verified for the mapping's life.
- Padding between pages is covered only by the scrub (no page digest includes it).
- Trust ends at the live counts (above) and at `.owner` records, which a writer of the store can forge as it can forge any file there.
- Only the cold start with no admission trusts; the recovery that follows a publication still walks the new generation (design S4).

## Design vs code

See `docs/status/namespace-s3.md`.
