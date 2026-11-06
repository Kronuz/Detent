# dense-pages--integration: journal

State: working
Quest: /Users/kronuz/code/Kronuz/Detent/docs/campaign/quests/dense-pages.md

## Doing

Finish ownership version-3 routing and safe old-format refusal/refetch; assemble the complete feature.

Adopted `phase/dense-pages-integration` at Detent:813c3ea; the original worktree stays parked. Prior contributor stopped at usage limit, with no live process or hold. The generated quest/task worktree is the active writer now.

## Next

1. Merge current complete codec/build/read/delta checkpoints into assembly.
2. Advance generation layout version to 3 and implement narrowly recognized old-format refusal/refetch under owner lock.
3. Add deterministic interrupted-copy/restart tests, update mechanism docs and run final composed proof.
4. Coordinate measured acceptance and whole-feature paladins before architect ruling.

## Blocked on

Nothing. Cross-component changes go through the lead; no per-step review queue.

## Evidence

Ownership version3 publication, prepared/named reads, census and recovery routing are implemented. Ownership decode recognizes old version2 metadata solely for refusal/cleanup; encode cannot produce version2. Exact independently hashed ownership fixture updated; qualification job pending. Safe obsolete-format refetch remains incomplete.

Detent:7618072 ports state commitment and root-prefix framing/tests. Standalone prefix truncation/empty/reserved/old-refusal checks passed historically.

Historical ledger: `docs/status/campaign-lead.md` on `phase/dense-pages-integration`. Historical evidence is scoped to its recorded code; it is not an automatically passing Campaign 2.0 job.

## Rulings I made

Campaign 2.0 adoption keeps one whole-feature landing. All builds/tests/measurements now use commit-pinned `campaign job submit --quest dense-pages --commit HEAD`; task agents must explicitly pass HEAD because the default quest ref is the assembly branch.

## Resumed qualification

Job 261105-211305-158ebe exposed a stale test: segment length 79 is legal with the 64-byte dense header. The exact lower boundary is now 63 rejected, 64 accepted. Production ownership rules are unchanged by this fixture correction.

## Publication blocker resumed

The composed compaction build failed because PagedNamedRoot still called removed physical_commitment for root metadata. It now uses metadata_commitment, matching snapshot and overlay descriptor construction. Targeted named-root/compaction qualification follows at this exact commit. No waiting on an owner decision.

The composed compaction compile exposed the final production physical_commitment caller in disk-store census. Its root checksum comparison now uses metadata_commitment. A repository-wide src/include search finds no remaining old caller. Delta proof 261106-061050-6ac155 passes leaves, overlay and dense assembly; delta-plan immutable graph validation remains open.
