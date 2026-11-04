> Status: CURRENT (2026-11-03) Ledger for namespace step S3 (D10 on today's store); trusted restart is blocked on the design gap below.

# namespace-s3

- Design: docs/design/namespace-delivery.md D10 (restart: trust prepared generations); criteria: docs/status/namespace-design.md, S3. C2 applies: random file ids, one directory. Digest-once machinery: `Detent:71db312` (`docs/mechanisms/recovery-digest-once.md`). Attribution: `docs/perf/agent-startup-2026-11-03.md` on `explore/agent-startup` (`Detent:e8d4e59`).
- Branch / worktree: `namespace/s3` at `.claude/worktrees/agent-aa9af415de8d08e6e`; base `Detent:ff0167d`.
- State: partly blocked. Criteria 1, 2, 4 and 5 need a trusted restart of the agent's store, which D10 does not specify enough to build (G1-G3 below). Criterion 3 (the scrub) does not depend on it and is being built.
- Done criteria (the lead's, verbatim from namespace-design.md; none ticked):
  - [ ] 1. Restart reads `storage.meta`, the table and the roots and `.read` files against their digests, with no pack read and no page hash; a counter proves zero page hashes at startup.
  - [ ] 2. First-touch checks: every page the agent's update path reads from a base generation is hash-checked against its parent reference before use.
  - [ ] 3. The scrub hashes every pack at `scrub_bytes_per_second`, continuously at that pace (ND-R8: an hourly pass at 1.51 GB costs about 0.2% of a core), and a test proves its pace within 10%.
  - [ ] 4. T5 over a 100K store: 100 single-byte flips in random pages. Restart is ready; first touch fails that read with a source failure and never returns a wrong value; the scrub reports every untouched flip within one pass; reads recover after repair. A corrupt table or `.read` refuses the store.
  - [ ] 5. A mutant that skips the first-touch check fails T5; sanitizers in separate builds.
  - Also: CI green on every job; a how-it-works note for trusted restart and the scrub; restart timed at 10K and 100K under `quiet strict`, before and after.

## Design gap (found 2026-11-03 reading the code at `Detent:ff0167d`)

D10 describes the reader's open (`PreparedReader::open`, `LazyPageAccess`): descriptor, root and `.read` checked, packs mapped on first use, no registry. The agent does not use that model. Its base generation is a `LeasedPagedRoot` holding a sealed `SegmentRegistry` that the recovery's graph walk fills in, and the update path depends on what the walk derives, not only on what it checks.

1. **G1, derived state has no source after a trusted restart.** The walk records, per segment, `SegmentReachability{pages, bytes}` and the 257-entry override histogram (`segment_registry.hh`, `paged_graph_validator.cc:382`). Nothing persists them: `.read` holds counts and physical bytes only (`reader_metadata.hh:9-13`). Consumers:
   - compaction selection: `live.pages && live.bytes < length / 2` (`coordinator.cc:332-336`);
   - which base segments the next recovery's registry carries: `base_reachability(i).pages` (`mapped_recovery_plan.cc:24,35`);
   - the next generation's own reachability, built from the base's by add and remove (`sparse_plan_finalizer.cc:45`, `paged_warm_validator.cc`), so a missing base value corrupts every successor's;
   - the maintenance and snapshot plans, which copy the histogram (`paged_maintenance_plan.cc:59-63`).
   The census also requires the registry to name exactly the segments the directory shows (`disk_store.cc:381-382`), and the registry's membership comes from the walk. The manifest digest D10.2 adds to `.read` (W12) authenticates a list but does not carry it; the list of (segment id, length, checksum) rows has to live somewhere a restart can read in O(segments) without a walk.
2. **G2, the update path reads base pages as raw spans.** `checked_page_bytes` (`paged_navigation.hh:17`) is a bounds check, not a digest check, and is the choke point of eight read sites: `paged_trie_overlay.cc:105`, `paged_contextual_reuse.cc:118`, `paged_cursor.cc:88`, `paged_warm_validator.cc:140,148`, `paged_dependency_walk.cc:105`, `paged_graph_validator.cc:113`. Today they are safe only because recovery verified every page. First touch needs a verified bit per page on each mapped segment (the reader's bitmap, `lazy_page_access.cc`) reachable from `SegmentInput`, which is a new field on a struct many validators construct.
3. **G3, warm validation inherits its base.** `WarmGraphValidator` validates only a successor's new segments and takes the prior registry's reachability as fact. With an unverified prior it is sound only if every base page it reads goes through G2's check, and only if G1's rows are the ones the build verified.

Two shapes close G1, both formats (the architect's):

- **A (recommended).** The build writes the segment rows and the histogram into a digest-bound record beside `.read` (a new file kind `generation-<id>.live`, or a variable-length `.read` v2; the reader's `prepared_reader.cc` parses `.read`, which reader/s5-s6 is changing, so a sibling file avoids that conflict). Restart reads it in O(segments), builds the registry by mapping each pack without hashing (`SegmentMapping::install` already checks size and file identity), and G2's bitmap makes first touch check each page once. Cost: about 64 bytes per segment (23 KB at 5M, 1,441 segments) plus 1 KB, a new name kind in `names.hh`, the census, the collector and publication's sync group.
- **B.** Recompute reachability in the background after ready. No format change, but the first compaction or republish waits for an O(N) structure walk (17.7 s at 5M, the attribution's arm A), and the registry's membership still needs a source, so it does not remove the need for rows.

Not built: neither shape, nor any change to recovery, the plans or the census. The lead rules; the architect should review the record's format (durability, crash order against the table, the reader's `.read`).

## Lead ruling (2026-11-03)
Shape A: `generation-<id>.live`, a digest-bound sibling published with the generation's files before the table that reaches it; not a `.read` v2. B rejected. G2: all raw-span read sites go through one first-touch accessor on the reader's verified-page bitmap (no second bitmap). G3 closed by G1 plus G2. The `.live` format and crash order go to the architect at landing.

## Done (commit `Detent:80028ff` on `namespace/s3`, built Release, not rebased)
- `PackScrub` (`pack_scrub.{hh,cc}`): paced pack hashing against owner checksums. `test_testing_pack_scrub` passes: fake clock exact, wall clock 2.396 MB/s for 2.4 MB/s (0.15% off, bound 10%), 100 flips in a real store reported as exactly the damaged packs.
- `.live` record (`live_record.{hh,cc}`), written by `PagedPublicationArtifacts::populate` between `.image` and `.read`; collector part 4 removes it (`disk_store.cc`); `GenerationFile::Live` in `names.hh`.
- `TrustedRestart::open` (`trusted_restart.cc`) wired into `RecoveryEngine::recover` for a cold start with no admission and no prior; absent `.live` falls back to the full recovery (a call for the owner: refuse instead?).
- First touch: `VerifiedPages` extracted from `LazyPageAccess` (shared, one bitmap class); `SegmentMapping::require_first_touch`; `SegmentInput::pages`; `checked_page_bytes` verifies with `paged::verify_page`; `PagedReadState::lookup` (a ninth raw-span site, not in the lead's eight) now goes through it.
- `paged::page_digests()` counter.
- `test_testing_trusted_restart` (99,990 to 100,000 knobs, three revisions) results so far: trusted equals full recovery in registry, reachability, histogram and counts at all three revisions; startup: 0 segment hash bytes, 320 root bytes, 0 page digests; first touch 238K page digests, 0 on a second pass; refusals: flips in `selected`, `.read`, `.read` digest, `.live` histogram, row and digest, a truncated `.live`, and a `.live` of another generation all refuse the store; a missing `.live` opens via the full path. T5: 100 flips, restart ready, 99,270 reads correct, 730 SourceFailure, 0 wrong values.

## Doing / failing
- The test aborts at its post-repair check (`test_trusted_restart.cc:249`): after `GenerationWriter(store).publish_json` over the damaged base throws, and the packs are restored, reads at revision 3 are not all correct. Unknown yet whether the refused publish left a revision-4 generation selected or something else; diagnostic prints were added (uncommitted edit to the test, not yet run). This is the update-path first-touch evidence for criterion 2, so it is not ticked.

## Next
1. Run the diagnostic build; fix the update-path finding (if a refused publish can leave a selected generation, that is a real bug to chase before anything else).
2. Revert check (skip the first-touch check, T5 must fail) and one mutant per claim; ASan+UBSan and TSan in separate builds.
3. Rebase onto `github/main` (`Detent:9a5a8c3`), run the full suite locally for fallout from the extra generation file (disk-store, collector and ledger tests may assert exact entry counts or restart rejection of corrupt packs), push, CI.
4. Restart timing at 10K and 100K under `quiet strict`, before (main) and after; counters.
5. Wire the scrub into the agent (where its ticks run is open) and report `failures()` in health.
6. Mechanism note `docs/mechanisms/trusted-restart.md` is a draft written from the code; re-check it against the final code.

## Blocked on
- Nothing now. Open calls for the lead: missing `.live` falls back to the full recovery instead of refusing (upgraded stores keep working); the scrub is a component, not yet driven by the agent.

## Design vs code
- D10 says the table binds the `.read` digest; today's descriptor does not, so `.live` binds to the `.read` file's own digest and `.read` binds to the root (the descriptor's checksum). Same chain, different carrier.
- D10 puts the manifest digest in `.read` (W12); the lead's ruling moved it into `.live` (rows are the manifest, with the trailing digest).
- Restart is O(segments), not O(namespaces): per segment one `.owner` read, one stat and one `mmap` (no pack read, no hash), as the census already did.
- The lead's eight raw-span read sites are nine: `PagedReadState::lookup` too.

## Log
- 2026-11-03: Branch `namespace/s3` from `github/main` (`Detent:ff0167d`). Host contention delayed every build by 20 to 60 minutes.
- 2026-11-04: Lead ruled shape A; implemented as above. Main moved to `Detent:9a5a8c3`.
