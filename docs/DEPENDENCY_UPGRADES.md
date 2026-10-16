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

## Reserved adoption passes

This file is the shared handoff between Detent integration work and library maintenance. Update the candidate revisions and findings here before starting an overlapping library change. Keep Detent's measured fleet baseline frozen; qualify upgrades in a separate build, then change pins in small commits.

| Pass | Reserved investigation time | Scope | Required exit gate |
| --- | --- | --- | --- |
| 1 | Next focused 60–90 minute pass after the measurement/pacing harness | Compatible leaf/runtime updates, starting with compressors and threadpool; adopt one dependency or tightly coupled set at a time | Public API/disk/wire diff, focused regression and sanitizer checks, daemon smoke, binary footprint and relevant before/after workload |
| 2 | Following 2–4 hour pass | Completion upgrade, including the cross-device fix below; retain Detent's existing storage backend | Local publication/recovery/cancellation and authority disk suites on macOS/Linux/FreeBSD, native versus fallback I/O accounting, no durability or resource regression |
| 3 | Separate 2 hour architecture/compatibility pass before implementation | Coherent journal/consensus revision set and DTC1 → DTC2 transition | Document coordinated restart or dual-codec mechanism, old-state recovery, mixed-version refusal behavior and rollback boundary; then schedule implementation and full qualification |
| 4 | Separately budgeted after measurements | Optional shared turn logs, batched device barriers, mappings and task groups | A demonstrated bottleneck, representative before/after data, lifetime/admission design and crash/power-loss model gates |

These are reserved investigation budgets, not completion promises. A failed gate produces a concrete finding here and leaves the qualified pin unchanged. A long-running benchmark may continue on the old immutable build while compatibility work uses another build; measurements used for capacity claims must record or exclude that contention.

## Coordination ledger

| Library / candidate | Owner | Finding and action | State |
| --- | --- | --- | --- |
| completion `86ca1a26c419238e851359a5199d9c8a04061d8e` | Library fix completed; Detent integration pending | macOS `sync_files` previously staged every file but full-flushed only the final descriptor's device. It now flushes one representative per distinct device. Public APIs and Linux/FreeBSD behavior are preserved. | Committed locally, not pushed by this session. macOS/Linux/FreeBSD 12/12 each, ASan/UBSan 12/12, focused TSan 1/1. Cross-device regression uses a syscall model; physical power loss remains untested. Detent pin unchanged. |
| compressors `6f6d7c05b74c3f790e23e4b994d945f12a0ea7aa` | Detent integration | Adds bounded zstd decoding and backend build switches. Check production fragment bounds and malformed/truncated streams before adopting; measure raw/zstd footprint and workload effects. | Pass 1 candidate; isolated daemon build and three-voter smoke pass as part of the candidate set. |
| threadpool `fdc0a1ef216aaf5357c92a6d09d6b60af65e66db` | Detent integration | Changes join settlement and adds TaskGroup. Verify authentic original completion and shutdown races; adopting the revision does not require replacing existing ownership mechanisms with TaskGroup. | Pass 1 candidate; isolated daemon build and three-voter smoke pass as part of the candidate set. |
| journal `a678e44e7fffd6b745bd89ac9589fdc15796be6e` / consensus `274a67441866f1976ccdafbfa23b99a596245242` | Joint architecture/integration | New journal headers require completion's `File::sync_data`. Consensus changes peer frames to DTC2. Upgrade this dependency stack coherently; do not interpret old disk fixtures as wire compatibility. | Pass 3 required; no pin bump or rolling-upgrade claim. |

The candidate build captured 27 immutable first-party library revisions. Its successful three-daemon build and 1,000-key/16-producer smoke establish basic compatibility for that combined candidate set, not an independently qualified pin bump for each row. The completion durability fix landed afterward and needs its own Detent integration run.

For each adoption, record old/new commit IDs, the source/API/format audit, exact test commands and results, measured effects, known limitations and the Detent commit that changes the pin. Coordinate file ownership with an active library maintainer before editing; never reset their work or point qualification builds at a changing sibling checkout. Publishing library commits and changing an application's dependency pin are separate actions.

## Application-side performance findings

The preliminary 500-session/128-exchange run passed checkpoint and full SDK verification, but spent 283.23 seconds around a 10.83-second measured write interval. Probe CPU was 9.54 seconds over the full process lifetime and 2.13 seconds during measurement. The qualification driver retained already-verified probes in the polling loop and queued operations could use the pre-checkpoint goal. Both paths are corrected in `55d6978`. The [paired rerun](../benchmarks/fleet-checkpoint-comparison.json) preserves the same fleet settings and reduces checkpoint plus SDK verification from 272.01 to 32.60 seconds. Full native lifetime changes from 283.23 to 73.58 seconds, CPU from 9.54 to 8.24 seconds, and peak RSS from 60.61 to 67.02 MB. Closed-loop write totals and background contention differ, so this is a checkpoint scheduling observation rather than an ingestion-throughput comparison. This is a benchmark scheduling issue, not evidence of a library or production-daemon performance defect.


The library maintainer's [C4 handoff](LIBRARY_BOUNDARIES.md) records later consensus pipeline, follower batching and control-priority work (`28e1b88`, `dd818a4`, `3f69530`, `367c89c`). The 27-library compatibility snapshot above predates those commits. Re-capture a coherent revision set before adoption, and wait for the documented DTC2 frame/tag boundary to settle; a passing build of `274a674` does not qualify the later C4 revisions.
