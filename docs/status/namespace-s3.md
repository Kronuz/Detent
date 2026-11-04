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

## Done
- Read D10, D8, T5, the attribution, the hash-once note and the recovery, registry, plan and census code. No code yet.

## Doing
- The scrub (criterion 3) as a library component with a deterministic-clock test and a real-clock pace test.

## Next
1. The scrub, its tests and its mechanism note.
2. On the lead's ruling for G1-G3: trusted restart, first touch, T5, the restart timing.

## Blocked on
- G1-G3 above, for criteria 1, 2, 4, 5 (the lead, then the architect for the record's format).

## Design vs code
- D10 says restart is O(namespaces) with packs opened on first use; the agent's code needs a registry and per-segment reachability that only the walk produces (G1).
- D10.2 puts the scrub in the agent; the agent's store runs as completion-driven state machines (`disk_store.cc`). Where the scrub's ticks run is open (see Next).

## Log
- 2026-11-03: Branch `namespace/s3` from `github/main` (`Detent:ff0167d`). Host load 20-28 on 16 CPUs, so no build yet.
