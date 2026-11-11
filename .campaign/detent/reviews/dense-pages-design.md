# dense-pages review: design

Commit: Detent:81a3232f1072e63980684f83478cc08a2dca411c
Verdict: CHANGES
Date: 2026-11-09

APPROVE: lands as is, at this commit's code. CHANGES: lands after each blocking finding is fixed
and confirmed by a job at the fixed commit. REJECT: the approach is wrong; the quest is re-planned.
An APPROVE lists no blocking finding; a CHANGES lists at least one.

The shape is right. One digest outside placement, bottom-up canonical packing, one packer that
both the full build and deltas drive (`PagedPacker::push_cluster`/`push_reference`), and a refusal
path that does not depend on the version are all sound, and nothing frozen in the format needs to
stay unfrozen. Three things block landing: maintained docs still describe DTPAGE02, the on-disk
`Reference` has two byte layouts, and the delta path has no mechanism note.

## Evidence I relied on

- `campaign status dense-pages` at 81a3232: A1-A13 met, with jobs. I took the format bytes and the
  model match from 261109-145754-fb0366 (synthetic and 100K) and 261109-145754-933d09 (5M). I did
  not rebuild either one.
- The security review (accepted) covers decoder containment. The correctness review (CHANGES:
  COR-1 confirmed, PERF-1 on A15) covers behavior on each path. I did not redo either one, and I
  read their code findings as settled.

## What I checked myself

Probe scripts are in `/Users/kronuz/code/Kronuz/Detent/.scratch/review-dense-pages-design/`. Each
exits 0 once its finding is fixed.

- `stale_docs.sh`, job 261109-171223-c1bcc9: 20 maintained-doc lines still describe DTPAGE02
  (DES-1).
- `reference_shape.sh`, job 261109-171151-b72512: the root encodes `Reference` differently from
  in-page child entries (DES-2).
- `delta_note.sh`, job 261109-171326-925d30: `docs/mechanisms/dense-pages.md` has no overlay or
  assembler section (DES-3).
- I read the design, model diff, mechanism docs and HOW_IT_WORKS against `paged_layout.{hh,cc}`,
  `paged_packer.cc`, `paged_page_validator.cc`, `paged_read_state.cc`, `lazy_page_access.cc`,
  `paged_navigation.hh`, `paged_trie_overlay.cc`, `paged_overlay_assembler.cc`,
  `paged_contextual_reuse.cc`, `paged_root_prefix.cc`, `ownership.cc`, `generation.cc`,
  `disk_store.cc` and `prepared_reader.cc`. I also read `quest/detent/reader-s6` at
  Detent:b1d281c, to see what its rebase onto this branch will change.

Answers to the named risks:

1. **Format.** One shape, apart from DES-2. The format is versioned three ways: the magic string,
   the root's version field, and the ownership record's `layout_version`. All reserved bytes are
   refused when nonzero. The refetch mechanism (`ObsoleteGeneration`, `RemoveSelected`, barrier,
   full copy) does not depend on the version. A later DTPAGE04 therefore needs no migration, only
   recognition of the v3 framing (DES-8). The things frozen are frozen on purpose: capacity C=4096
   under the canonicity check (D6), one-byte prefix lengths (Limit 6) and the 255-page height.
2. **Docs.** `docs/mechanisms/dense-pages.md`, `verified-page-bitmap.md` and
   `recovery-digest-once.md` describe the code in present tense. The refetch path, its barrier and
   collection have a note (`dense-pages.md#refusal-and-reclamation`, `sync-groups.md:17`).
   Staleness elsewhere is DES-1. The delta overlay bounds have no note (DES-3).
3. **Parallel shapes.** The DTPAGE02 build path and `paged_page_builder` are gone. The only v2
   code left is recognition for refusal and cleanup. What remains duplicated is DES-2, DES-5,
   DES-6 and DES-7.
4. **Reader R1/R2.** The quest changes no reader API: `include/detent_local/reader.hh`, the C ABI
   and the client sources are untouched. It adds no defaults and no override path, so R1 and R2
   hold. DES-11 is a status-code question, not a surface change.
5. **Consumers.** Nothing blocks namespace-s4-s10. Derived ids touch only placement, which is
   outside every digest, so T8 needs no format change. Nothing blocks legacy removal either: the
   v1 `legacy` context predates this quest and is untouched. reader-s6 has a mechanical rebase
   ahead of it and carries the hit-compare cost in its T1 gate (DES-10).

## Findings

| ID | Blocking | Kind | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| DES-1 | yes | defect | Maintained docs still describe DTPAGE02: physical and logical digests, 80-byte headers, 88/96-byte references, one bit per page, the deleted page builder. A12 requires the mechanism and public docs to describe the single format. HOW_IT_WORKS §4 and §5, the file this quest edits, still say first touch "checks its physical and logical digests" and that a warm lookup is "one bit test and the bytes", which omits the header compare. The website copy was not updated at all. | Job 261109-171223-c1bcc9, 20 lines: `docs/HOW_IT_WORKS.md:109,127`, `docs/FAST_CLIENT_ARCHITECTURE.md:21`, `docs/INCREMENTAL_PUBLICATION_ARCHITECTURE.md:30,42,100,206,212`, `docs/mechanisms/read-counters.md:25`, `docs/mechanisms/trusted-restart.md:66,70`, `website/src/content/docs/architecture/how-it-works.md:212-235,289-290,373`. Not matched by the probe: `HOW_IT_WORKS.md:111` (warm lookup) and `:123` (writes "definition pages, the branches above them"). | Rewrite these lines in present tense for DTPAGE03, and mirror the HOW_IT_WORKS page section into the website page. `stale_docs.sh` exits 0. Leave dated measurement notes (`docs/perf/*`) as they are. |
| DES-2 | yes | defect | One type, two on-disk layouts. The root's tree reference at [160,225) is 65 bytes: segment, offset, length, keys, height, digest. An in-page child is a 48-byte content entry (digest, keys, length, height, 3 zero bytes) plus a 20-byte placement entry (segment, offset), so keys and length are even swapped. The design gives no reason for the second shape: D3 just states 65 bytes, and Alternatives does not cover it. The root has 95 reserved bytes, so the uniform 68-byte form costs nothing. Fixing it after landing takes a DTROOT04 and a refetch of every store. Separately, `encode_reference` is also called and its result discarded, as a validator. | Job 261109-171151-b72512. `include/detent_local/paged_layout.hh:14,73-77`, `src/detent_local/paged_layout.cc:156-212,359,382`, `paged_root_prefix.cc:95-106`. Validate-only calls: `paged_navigation.hh:22`, `paged_page_validator.cc:49`, `paged_dependency_walk.cc:57`, `paged_contextual_reuse.cc:77`, `paged_packer.cc:248,595`, `paged_segment_builder.cc:77`. | (a) Store `content ‖ placement` (68 bytes) at [160,228), require [228,320) to be zero, delete `encode_reference`/`decode_reference`, expose `validate(const Reference &)` for the validate-only callers, and update D3, the root-prefix checks and the exact root fixtures. `reference_shape.sh` exits 0. (b) Or keep the 65-byte form, with a cost case recorded in D3 and the owner's sign-off. That closes by ruling, not by the probe. |
| DES-3 | yes | gap | The delta path is a new mechanism with no how-it-works note. Its parts are the overlay's bounded inline walk (visits at most `header.nodes`, forward slots, a stack 202 deep), corresponding-path reuse with one extent per occurrence, off-path seals, and placement-only relocation. They are described only as intent (design D7/D8) and in one HOW_IT_WORKS sentence. HOW_IT_WORKS §5 sends readers to INCREMENTAL_PUBLICATION_ARCHITECTURE, which describes v2. A maintainer cannot learn the containment contract COR-1 restored from the docs. | Job 261109-171326-925d30. `src/detent_local/paged_trie_overlay.cc:20-70,115-150`, `paged_overlay_assembler.cc:132-160`, `docs/HOW_IT_WORKS.md:93,123`. | Add a "Deltas and relocation" section to `docs/mechanisms/dense-pages.md` covering the overlay bounds, the reuse identity, single ownership of extents, off-path seals, and relocation computing no page digest. Point HOW_IT_WORKS §5 at it. `delta_note.sh` exits 0. |
| DES-4 | no | nit | "Integration boundary" describes plan steps (S1/S2/S3 validators, "the S1 fuzzer") rather than the code. It also understates fuzzing, which covers the walk and validators too (A4). | `docs/mechanisms/dense-pages.md:21-23` | Describe what each target and function checks. |
| DES-5 | no | nit | The D5 sealing choice (lowest height, then largest size, then first label) is written twice: once in the packer and once in the canonicity replay. If they diverge, builds are refused, so nothing is lost silently, but there are two copies of one rule. | `paged_packer.cc:417-427`, `paged_page_validator.cc:78-110` | Share one comparator. |
| DES-6 | no | gap | About 11 in-page walkers each re-derive inline-slot progress. Their checks differ: `slot < end \|\| slot <= offset` in `paged_read_state.cc:101` and `paged_cursor.cc:141`, `slot < end` only in `paged_trie_overlay.cc:31,147`, and the key-driven walkers rely on consuming key bytes. COR-1 was one copy missing a bound. | `git grep decode_node( src` at 81a3232 | Follow-up: one bounded inline-step helper that every walker uses. |
| DES-7 | no | nit | There are two first-touch implementations, and each now repeats the header compare: `LazyPageAccess::resolve`, which checks leaf semantics, and `checked_page_bytes`, which does not. This predates the quest. | `lazy_page_access.cc:196-225`, `paged_navigation.hh:20-40` | Merge them when reader-s6 rewrites `resolve`. |
| DES-8 | no | nit | Obsolete-version recognition is the literal `2` in four places, plus a v2-only prefix validator. A version bump has to find all of them. | `ownership.cc:88`, `generation.cc:355`, `disk_store.cc:1276`, `paged_layout.cc:227,363`, `paged_root_prefix.cc:41` | Use one `obsolete_paged_layout(version)` predicate. |
| DES-9 | no | nit | Several names outlive the one-digest format. `page_digests()` is an alias of `hash_counts().page_calls` that only one test uses. `StateCursor::logical_commitment`, `PreparedGeneration::logical` and `maximum_leaf_body_bytes = page_bytes - 80` keep v2 names and numbers. | `paged_layout.cc:123`, `test_trusted_restart.cc:88-195`, `paged_layout.hh:17` | Drop the alias and use one name, `state_commitment`. Keep the 80 only with its reason. |
| DES-10 | no | gap | The design's status and landing order are stale. It is still "DRAFT … implementation pending". Its 2026-11-05 scope note was superseded by D4. D10 and the landing table say S6 and S3b land first, but both are parked and Dense lands first, so A8's "landed S6 interfaces" is vacuous. The reader-s6 rebase touches `lazy_page_access.cc:147,228` (`page_header_bytes` is now 64 and should be `minimum_page_bytes`), `:233` (the bitmap test now has to follow the header compare), `prepared_reader.cc:172` (`layout_version == 2`), `:232` and `prepared_reader.hh:10` (`root.logical`). Its T1 gate will carry D4's hit compare. | `docs/design/dense-pages.md:1-17,188,292-299`, `quest/detent/reader-s6` at b1d281c | Lead: update the design's status and landing order at landing, and tell reader-s6 about the rebase points. |
| DES-11 | no | gap | A reader library that meets a store with version-2 ownership (library upgraded before the agent) reports `ReadSourceFailure`, which reads as damage. A v1 store gets `GenerationUnavailable`. Either way the caller gets its default back, so only the status differs. | `src/detent_local/prepared_reader.cc:144-146` | Owner's call: (a) keep it, since the project is pre-release (G1), or (b) map version 2 to `GenerationUnavailable` as v1 is. |

## Not run

- No build, test, sanitizer, fuzz or benchmark run. Code behavior is the correctness and security
  reviews' ground, and A15/T5 is still the lead's.
- I did not check correctness of the walkers in DES-6 beyond whether each one terminates.
- The design's numbers were not rechecked against the model. I took A3/A5 jobs as evidence for
  that.
- T8 file identity, namespace-delivery's derived ids, and other platforms were not examined.
