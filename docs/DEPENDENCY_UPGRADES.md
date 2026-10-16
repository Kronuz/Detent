# Pending library upgrades

**Date:** 2026-10-15

Detent pins its direct and transitive dependencies in `cmake/dependencies.json`. Qualification uses immutable archives of those revisions. The sibling repositories may advance independently; their current working directories are not reproducible build inputs.

## Consensus peer protocol

The qualified consensus revision `b7073bbe4d94e66dce526f193b0dd81a57cdb661` uses DTC1 peer control frames. Candidate `274a67441866f1976ccdafbfa23b99a596245242` uses DTC2. Replacing the dependency alone does not support a rolling upgrade with old voters. Adoption requires either a coordinated cluster restart or an explicitly designed dual-codec transition, with mixed-version rejection and recovery tests before release.

The candidate also adds dynamic membership and shared turn-log support. Their availability in the library does not imply Detent exposes or qualifies them. Existing fixed-voter disk compatibility fixtures cover a different boundary from peer wire compatibility.

## Storage and other additions

Completion adds `File::sync_data`, `Directory::sync_files` and read-only file mappings. Journal and consensus add a shared turn-log backend. These APIs need workload and durability qualification before Detent adopts them. In particular, a fix to the new shared manifest decoder is not evidence that Detent's existing named-file recovery path has the same defect.

Other candidate changes include bounded zstd decompression, thread-pool join behavior and owner-scoped task groups. Each adoption must preserve the public behavior Detent relies on and include the relevant focused and end-to-end tests. Performance claims require measurements against the recorded qualified revision.

An independent Release build of `detentd`, `detent-agent` and `detent-relay` passes against immutable snapshots of the candidate sibling revisions. A three-voter writer smoke also passes with 1,000 keys and 16 producers, including final state and receipt reconciliation. This establishes basic build and writer compatibility for a uniform new-version cluster. It does not qualify mixed-version peers, a shared backend migration or the full production gate. No candidate revision or new backend is part of the qualified baseline.
