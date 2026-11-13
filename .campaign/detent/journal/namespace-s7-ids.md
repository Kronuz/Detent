# namespace-s7-ids: journal

State: ready for review
Quest: .campaign/detent/quests/namespace-s7-ids.md (the lead's; never edit it)

Keep this current and commit it with the work, so a fresh contributor can continue. `campaign
status` reads State and the first step under Next. State is one of: working, waiting on jobs <ids>,
ready for review, fixing <findings>, blocked on <who>: <what>.

## Doing

Durability review CHANGES (88987d54) folded in at e101c49f: F1, F2, F4 fixed; F3, F5 noted.
Jobs queued at e101c49f (lead resumes on them): native 261112-235321-9e6f06, pins
261112-235321-c15cca, A2+A3 261112-235323-bfa303, F1 mutant 261112-235326-77c70c, F2 mutant
261112-235330-8a834b; flake probe at main 261112-235559-3c947d. Locally: test_derived_names
passes; `T7_GROUP=orphans` 718 cases, all 7 orphan Removes cut; both mutants killed (F1 at the
new check, F2 at `open_and_recover`'s selected check: a cut, not the publication, kills it).

Suspected flake, not this quest's: native 261112-232904-0ca1be (at 2641202d, the first fix
commit) failed only `test_local_paged_publication_artifacts` ("invalid paged artifact operation
accepted", the pressure path's `rejects<StoragePressure>`). It passed at 88987d54 (40d6a2) and
passes 60/60 serially here; under 40-way contention it aborts 3 of 200 runs with that message.
None of its inputs depend on the derivation's bytes. Probe 261112-235559-3c947d runs the same
contention at main 8d07ecad; if it fails, the flake is main's (the lead's).

## Next

1. When the jobs finish, paste `campaign brief`'s Evidence table below; then
   `campaign accept namespace-s7-ids durability --confirm F1=261112-235321-9e6f06
   F2=261112-235321-9e6f06` (the lead's). On a failure, fix it and rerun only that job.

## Confirm mapping (durability review)

| Finding | Suite job | Fails without the fix |
|---|---|---|
| F1 | 261112-235321-9e6f06 (and A2 job 261112-235323-bfa303) | 261112-235326-77c70c (`s7-f1-cluster-omitted.patch`, killed by `test_local_derived_names`) |
| F2 | 261112-235321-9e6f06 (and A3 job 261112-235323-bfa303) | 261112-235330-8a834b (`s7-f2-orphan-remove-selected.patch`, killed by the orphan sweep alone, `T7_GROUP=orphans`) |

## Review findings (durability, 88987d54)

- F1 fixed: `namespace_cursor` hashes `cursor.cluster` into the D88 incarnation
  (`src/detent_local/derived_ids.cc`), so a trusted reset changing only the cluster derives new
  names; D8.1's `derive_generation` is unchanged. Test: `ordinals()` in
  `tests/detent_local/test_derived_names.cc`; mutant `tests/detent_local/mutants/s7-f1-cluster-omitted.patch`.
- F2 fixed: `orphan_cuts` in `tests/detent_local/test_publication_power_cut.cc` republishes over the
  durable state just before the cut publication's `selected` Replace (its generation files and
  segments), cuts at every event in the four modes and kills at every boundary, and checks every
  orphan Remove was cut (`removes == rerun.removed.size()`) and that the Removes cover generation
  files and segments. Runs in the full test and alone under `T7_GROUP=orphans`. Mutant
  `tests/detent_local/mutants/s7-f2-orphan-remove-selected.patch` (an orphan lease's unlink hits
  `selected`): the sweep kills it.
- F4 fixed: `docs/HOW_IT_WORKS.md` (`.owner`, `.read` rows) and `docs/mechanisms/trusted-restart.md`
  (`.read` binding, `.owner` fields) describe DTSGOWN2 and DTREAD02.
- F3 (follow-up, no code here): preflight's orphan unlink skips the lease protocol; a revisited
  cursor (reset A, B, A at one revision) can unlink a `.lease` a reader still holds. Fails safe
  (source failure, pages verified). Options: keep it, or try `ExclusiveLease` on an orphan's
  `.lease` and reject when held. Owner's or lead's call.
- F5 (follow-up, doc line only): a `PagedNamedRoot` without a cached certificate carries ordinal 0;
  `begin` refuses a build on it at a selected ordinal above 0. One line in `docs/status/namespace-s7.md`.

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
| A1 | <native 261112-235321-9e6f06 queued>, pins 261112-235321-c15cca; grep 261112-210808-4881bd |
| A2 | <261112-235323-bfa303 queued>; mutants 261112-210819-bb52c5 (ordinal reused), <261112-235326-77c70c queued> (F1) |
| A3 | <261112-235323-bfa303 queued>; mutant <261112-235330-8a834b queued> (F2) |
| A4 | 261112-210808-4881bd |

Superseded: 261112-210812-89eb23 and 261112-210816-a5eb16 (built without configuring), replaced
by 261112-235323-bfa303.

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
   (`test_publication_power_cut.cc`) cuts power at every publication event in windows 1, 2 and
   128, and its orphan sweep (`orphan_cuts`) at every event of a republication over a cut
   publication's durable orphans, every orphan Remove included, in all four modes plus a kill at
   each boundary; each cut recovers to the last acknowledged generation or the one before.
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
3. D88 cursor mapping: incarnation = SHA-256("detent.agent-store.incarnation", cluster, group,
   restore_epoch, scope)[:16] (cluster added for F1), position = revision, until namespace-s6-cutover.
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
10. F1: the cluster enters the D88 mapping (the incarnation), not `derive_generation`, which keeps
    D8.1's input list; after namespace-s6-cutover the namespace incarnation is drawn per namespace.
11. F4's two docs (`docs/HOW_IT_WORKS.md`, `docs/mechanisms/trusted-restart.md`) are outside the
    file set; edited on the lead's instruction for F4.
