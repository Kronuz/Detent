# namespace-s7-ids: journal

State: working
Quest: .campaign/detent/quests/namespace-s7-ids.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Evidence jobs queued at 88987d54 (main merged): verify native 261112-210758-40d6a2, pins
261112-210758-04a7c0; A1/A4 grep 261112-210808-4881bd; A3 crash model 261112-210812-89eb23; A2
test 261112-210816-a5eb16; A2 mutant 261112-210819-bb52c5. Local full suite before the merge:
283/283 passed (`.scratch/build`).

## Next

1. When the jobs finish, paste `campaign brief`'s Evidence table below and set State to ready for
   review (or fix what failed and rerun only that job).

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

Generation, segment, `.read` and `.owner` names are derived as D8.1 says, in one place
(`include/detent_local/derived_ids.hh`, `src/detent_local/derived_ids.cc`); no StorageIdentity
enters a segment id, `.owner` (DTSGOWN2) or `.read` (DTREAD02). `.lease` and storage.meta keep it.

1. Every builder derives its own name with `next_generation`; DiskStore rejects a publication whose
   name and ordinal are not that derivation over the selected generation.
2. A rebuild at an unchanged head takes ordinal r + 1 (descriptor bytes 164..168), so a name is
   never reused: `tests/detent_local/test_derived_names.cc`, mutant
   `tests/detent_local/mutants/s7-a2-ordinal-reused.patch`.
3. Preflight unlinks an orphan at a derived name and writes it again (D8.4); the crash model
   (`test_publication_power_cut.cc`) cuts power at every primitive including that Remove and checks
   recovery to the last acknowledged generation or the one before, and that orphans were relinked.
4. PagedSemanticLimits has no default bounds (static_assert in test_derived_names); callers pass
   the store's recovery limits or `paged_format_semantics`. Static checks:
   `tests/detent_local/derived_names_grep.sh`.
5. Docs: `docs/status/namespace-s7.md`, "Derived names in the managed store".

Out of the file set (msg-41), in their own commits: 0f110c25 and the s4-a1 delta mutant commit.

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
8. The segment builder counts the whole derivation input as one metadata hash
   (`segment_derivation_bytes` = 26 + 16 + 8); test_dense_fullbuild expects that count.
9. Pre-existing, not this quest's: 9 mutant patches no longer apply on main either
   (cor1-unguarded-parent, s3b-k1, s3b-k2, s3b-k4 x4, s3b-m6, s4-a3); checked with `git apply
   --check` against a main checkout at the quest's base.
