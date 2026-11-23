# namespace-s7-ids review: durability

Commit: Detent:88987d544ee6
Verdict: CHANGES
Date: 2026-11-12

APPROVE: lands as is, at this commit's code, with no blocking finding. CHANGES: lands once each
blocking finding's fix and its test pass the suite at the fixed commit; with only nits, an APPROVE.
REJECT: the approach is wrong; the quest is re-planned.

## Evidence I relied on

1. 261112-210808-4881bd (A1/A4 static checks, exit 0): no storage identity in `SegmentOwnership`,
   `ReaderMetadata` or the segment builder; derivation only in `derived_ids`, the segment builder and
   the agent store; no `PagedSemanticLimits{}`, no default bounds.
2. NATIVE_PLACEHOLDER
3. CRASH_PLACEHOLDER

## What I checked myself

Scripts under `.scratch/review-namespace-s7-ids-durability/` in the home checkout.

1. **Name derivation (risk 1).** Probe job 261112-212907-5c5ff2 (`cluster_collision.sh`,
   `cluster_collision.cc`) compiles `derived_ids.cc` alone and asks `next_generation` for a reset
   whose cursor differs from the selected one only in its cluster, at the same revision: it derives
   the selected generation's own name (exit 1). See F1. By reading: a rebuild at an unchanged head
   takes r + 1 from the durable `selected` (bytes 164..168, `generation.cc:80,97`), so along the
   selected chain at one cursor the ordinal only grows; a delta and a compaction of one head differ
   in base (`paged_plan.cc:40-43`); maintenance and repair at one head derive one name, which is safe
   because only one of them can be published before the ordinal moves and the other's files are
   orphans; segment ordinals are the output's own segment count within one generation
   (`paged_overlay_assembler.cc:324`, `paged_snapshot_builder.cc:229`). Two agents cannot share a
   directory: the owner lock (`storage_session.cc:18-20,139`) refuses the second.
2. **Publication crash safety (risk 2).** Preflight orphan Remove (`disk_store.cc:620-649`) runs
   before any file is written; it is volatile until the owners-and-lease group's directory sync, and
   the removed name is never reached by the selected generation (`reached`, `disk_store.cc:779-794`),
   so a cut before or after that sync recovers to the generation before. The crash model's state
   model treats Remove as a volatile entry change (`test_publication_power_cut.cc:382-384`) and
   verifies each cut through `open_and_recover`, which runs the cold graph validator over the
   recovered generation (`generation.cc:476-490`): it checks recovered bytes, not only that recovery
   returned. Its reach over the Remove is sampled, not exhaustive: F2.
3. **Readers (risk 3).** Every `.read` and `.owner` parse goes through
   `decode_reader_metadata`/`validate_reader_metadata_prefix` and `decode_segment_ownership` with the
   moved offsets (`reader_metadata.cc:34-100`, `ownership.cc:96-120`): the prepared reader
   (`prepared_reader.cc:152-160`), trusted restart (`trusted_restart.cc:50-60`), recovery
   (`generation.cc:416-420`), the collector (`paged_segment_collection.cc:615-620`) and reclaim's
   partial prefixes (`disk_store.cc:1378-1416`, ordinal bytes skipped only against the image header,
   which cannot hold them). The one storage-identity check left is `.lease` against `storage.meta`
   (`generation.cc:525-528`). Readers and the collector agree on names: both build them from
   `generation_name`/`SegmentNames` over the descriptor and owner ids, and the collector's
   `same_generation` (`paged_segment_collection.cc:370,381`) is equivalent to equality because the
   id binds the ordinal.
4. **A4 (risk 4).** `PagedSemanticLimits` has only a two-argument constructor
   (`paged_graph_validator.hh:21-25`; `static_assert` in `test_derived_names.cc:14`). Source callers
   pass the recovery limits (`generation.cc:447,483`, `candidate_paged_validator.cc:68`,
   `paged_segment_collection.cc:479`) or `paged_format_semantics` (`sparse_plan_finalizer.cc:115`,
   guarded by `test_warm_semantic_bounds.mutant.patch`; the store's string bound is enforced at
   admission, `disk_store.cc:1757`); `benchmarks/paged_snapshot_bench.cc:66` passes the format bound.

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| F1 | yes | defect | The derivation omits the cursor's cluster. The D88 incarnation hashes group, restore epoch and scope only, and `derive_generation` takes the incarnation and position. A trusted identity reset may change the cluster alone (`same_identity` compares it; both are operator-configured, `configuration.cc:155-156`), so a reset to another cluster at the selected revision derives the selected generation's own name; preflight finds it reached and rejects every retry, wedging the reset until the head moves. Against an older generation the same collision instead takes the orphan path over another cluster's files. | Probe 261112-212907-5c5ff2 (exit 1: same name, ordinal 0 vs 0); `derived_ids.cc:58-60,76-79`; `disk_store.cc:621-628,1737,1836` | Hash `cursor.cluster` into the D88 incarnation (or into `derive_generation`). Test: the probe's case in `test_derived_names.cc` (names differ for cursors differing only in cluster). |
| F2 | yes | gap | A3 says a power cut at every primitive of publication; the crash model never cuts at every orphan Remove. The full sweep (every event, windows 1, 2 and 128) publishes over a clean seed and issues no Remove; Removes occur only in the window-2 restart cycles, which start at every fifth boundary and cut every third rerun event in two of the four modes. By reading the Remove states are safe (point 2 above), but the criterion's claim, and the journal's "cuts power at every primitive including that Remove", are unproven for the one primitive this quest adds. | `test_publication_power_cut.cc:653,708,728` | One sweep that publishes over a directory holding a cut publication's durable orphans (generation files and segments) and cuts at every event in all four modes and the kill mode, checking each Remove was reached. |
| F3 | no | gap | Preflight's orphan unlink skips the lease protocol: `reached` consults only the selected generation, while reclaim takes `ExclusiveLease` before deleting. A revisited cursor (a reset A, B, then A at the same revision, full copy, ordinal 0) re-derives a once-selected generation; if a reader still leases it, preflight unlinks its `.lease` and packs and writes new inodes, the reader's lock stays on the unlinked lease, and a later collection may delete packs that reader still opens lazily (a source failure, never wrong data: pages are verified). D8.4 is silent on leases. | `disk_store.cc:779-794` against `:1035-1043` | Owner's or lead's call: keep it (rare, fails safe), or try `ExclusiveLease` on an orphan's `.lease` and reject the publication when it is held. |
| F4 | no | nit | Two docs still describe the storage identity inside the moved records. | `docs/HOW_IT_WORKS.md:47` (`.owner`: "storage identity, id, length"); `docs/mechanisms/trusted-restart.md:42` (`.read` "carries the storage identity and is checked against `storage.meta`") | Update both to the DTSGOWN2 and DTREAD02 layouts. |
| F5 | no | nit | A `PagedNamedRoot` without a cached certificate carries ordinal 0, the image and lease not holding it. A builder based on it at a selected ordinal r > 0 would be refused by DiskStore's selection equality: fail-safe, never a collision. | `paged_named_root.cc:409-417`; `disk_store.cc:1727-1732` | None needed; worth one line in the status doc. |

## Not run

1. No sanitizer or Linux run (CI's); the crash model's Linux sync semantics are CI's.
2. No DiskStore-level reproduction of F1 or F3: F1's DiskStore rejection follows from
   `disk_store.cc:621-628` by reading, and F3 needs a reader holding a lease across two resets.
3. The new-layout `AgentStore` publication path beyond its type renames (it is not live under the
   old `selected` descriptor; namespace-s7-store owns it).
4. The mechanical signature follow-ups outside `src/detent_local` (0f110c25 and the mutant patches).
