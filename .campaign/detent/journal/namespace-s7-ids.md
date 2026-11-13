# namespace-s7-ids: journal

State: working
Quest: .campaign/detent/quests/namespace-s7-ids.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Fixing test compilation after the signature changes (builders derive their own names; no
StorageIdentity in segment ids, `.owner`, `.read`; PagedSemanticLimits has no defaults). Build:
`.scratch/build` (configured with -DDETENT_BUILD_BENCHMARKS=ON); errors in `.scratch/errors.txt`.
Mechanical rewrites live in `.scratch/dropargs.py` and `.scratch/validators.py`.

## Next

1. Finish test compilation, then fix runtime failures: tests publishing through DiskStore must use
   derived names (`l::next_generation`), descriptor/reader-metadata byte offsets moved.
2. test_publication_power_cut.cc: republish cycles reuse the derived name and exercise orphan
   unlinking (A3).
3. A2 test (rebuild at an unchanged head takes r + 1, never a reused name) and its mutant.
4. docs/status/namespace-s7.md; journal Result; jobs: verify, grep A1/A4, crash model A3, A2.

## Blocked on

msg-41 to the lead: the file set lacks tests/detent_delivery/test_repair.cc,
test_bundle_maintenance.cc, test_coordinator.cc, test_bootstrap_retention.cc,
benchmarks/paged_recovery_bench.cc, paged_collection_bench.cc, local_image_bench.cc and the
s4-a1 mutant patches; they no longer compile against the new signatures. Recommendation: widen the
file set; those edits sit in one separate commit, ready to keep or drop.

## Evidence

Jobs prove criteria by their tags (`--for A1`); paste `campaign brief`'s generated table here.
Write the rows of [artifact], [decision] and [manual] criteria (a `path`, a decision id, who
checked what), and answer each message by its id (msg-N).

| Criterion | Evidence |
|---|---|
| <A1> | <from `campaign brief`> |

## Result

<When ready for review.>

## Rulings I made

1. Every name comes from `next_generation` in `src/detent_local/derived_ids.cc` (D8.1, D8.2), and
   each builder derives its own (ImageBuilder, PagedSnapshotPlanBuilder, PagedMaintenanceBuilder,
   PagedPlanBuilder, PagedDeltaPlanBuilder, PagedCompactionBuilder); no caller passes a
   GenerationId. The coordinator's RAND_bytes ids are gone. DiskStore rejects a publication whose
   name and ordinal are not the derivation over the selected generation.
2. A full copy (zero base) is any plan with `snapshot()`: snapshots, repairs, trusted resets and the
   maintenance (migration) rebuild. Compaction, delta and progress are base-relative.
3. D88 cursor mapping: incarnation = SHA-256("detent.agent-store.incarnation", group, restore_epoch,
   scope)[:16], position = revision, until namespace-s6-cutover.
4. The ordinal persists in the descriptor's formerly reserved bytes 164..168. A PagedNamedRoot
   rebuilt from the lease and the image cannot know it, so certificates compare with
   `same_generation` (all but the ordinal) and adopt the cached descriptor.
5. A flat image is always forward of the selected head, so its ordinal is 0.
6. Publication preflight (D8.4): an existing derived name the selected generation does not reach is
   an orphan; it is unlinked (its usage credited to the ledger) and written again. A reached name,
   or an unsafe entry, rejects the publication. The unlink needs no barrier of its own: the
   Replace's directory sync orders it before the new selection is durable.
7. `paged_format_semantics` (graph validator header) is the format's bound, the one fallback
   callers name explicitly; the store's bound comes from its recovery limits.
