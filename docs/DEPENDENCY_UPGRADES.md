# Pending library upgrades

**Date:** 2026-10-15

Detent pins its direct and transitive dependencies in `cmake/dependencies.json`. Qualification uses immutable archives of those revisions. The sibling repositories may advance independently; their current working directories are not reproducible build inputs.

## Active adoption pass, October 17

The user requested adoption of the latest libraries, before/after benchmarks and an audit for duplicated mechanisms. The [validated adoption blueprint](LIBRARY_ADOPTION_ARCHITECTURE.md) governs this pass. Published default-branch heads differ for 27 of 28 Kronuz pins in the [new inventory](../benchmarks/library-adoption-candidates-20261017.json); older candidate rows below retain their historical qualification only. Freeze the transport application baseline before changing pins, capture preliminary old-library measurements, then compare old/new builds from identical application sources in alternating pairs.

The current published completion head is `e6c28aaef09c`; the corrected per-device macOS synchronization exists only at local `4848450`. Individual-sync pin adoption can proceed independently; general cross-device `sync_files` and shared-log adoption wait for the correction and their durability gates. Journal's working tree has unrelated changes: use committed archives and preserve that work. Published HTTP/reactor do not replace Detent's persistent client or relay overload fixes. Pin adoption, extracting duplicated code and enabling new mechanisms are separate measured changes.

Compilation and load runs are held again at the user's October 17 request while another benchmark finishes. The preliminary baseline harness and its current durable sample are suspended; exclude that interrupted sample and repeat it after the all-clear. Source review and documentation may continue. No dependency pin has changed.

Source preparation now lives in separate detached checkouts under `.scratch/library-adoption-20261017/`: `candidate` contains the 27 published pin updates, `reuse` contains independent helper consolidations, and `comparison-base` retains the old pins. These changes are not qualified or landed. Both comparison arms explicitly preserve Detent's existing `NodeOptions` timing values because the new consensus defaults change election timing. The existing preliminary binaries predate that source clarification; rebuild both arms from the same application source before the paired acceptance runs.

Group F has an additional source-derived recovery-resource gate. Latest consensus reads checkpoint bundles and historical logs against library ceilings rather than the host's configured limits. Detent's configured checkpoint reservation grows only from 68,485,520 to 68,491,764 bytes under default limits; retain that sizing call. Add an optional retained-state limit to consensus recovery before claiming the old resource envelope: reject oversized descriptors before full-buffer allocation, enforce decoded entry/payload bounds before growth, and check aggregate retained bytes/entries during replay. Absence must preserve generic lowered-limit recovery behavior. Stage any correction separately from the library maintainer's working trees; no journal edit is required. No reproduction or runtime regression is claimed during the hold.

## Consensus peer protocol

The qualified consensus revision `b7073bbe4d94e66dce526f193b0dd81a57cdb661` uses DTC1 peer control frames. Candidate `d7192f728b98fa42866e5829818bb19b11ae89a9` uses DTC2. Replacing the dependency alone does not support a rolling upgrade with old voters. Adoption requires either a coordinated cluster restart or an explicitly designed dual-codec transition, with mixed-version rejection and recovery tests before release.

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
| scheduler `3ec5d7d` / random | Detent integration, coordinated with library maintainer | Scheduler now links `random::random` instead of compiling vendored definitions. Detent already makes `random_lib` available before reactor/http/cluster, which can populate scheduler. Preserve that order and verify one shared target when adopting the pin. | New Pass 1 candidate reported in the maintainer handoff; not part of the captured compatibility build. Source ordering checked; no compilation or duplicate-symbol reproduction during the benchmark hold. |
| journal `a678e44e7fffd6b745bd89ac9589fdc15796be6e` / consensus `d7192f728b98fa42866e5829818bb19b11ae89a9` | Joint architecture/integration | New journal headers require completion's `File::sync_data`. Consensus changes peer frames to DTC2. Upgrade this dependency stack coherently; do not interpret old disk fixtures as wire compatibility. | Pass 3 required; no pin bump or rolling-upgrade claim. |

The candidate build captured 27 immutable first-party library revisions. Its successful three-daemon build and 1,000-key/16-producer smoke establish basic compatibility for that combined candidate set, not an independently qualified pin bump for each row. The completion durability fix landed afterward and needs its own Detent integration run.

For each adoption, record old/new commit IDs, the source/API/format audit, exact test commands and results, measured effects, known limitations and the Detent commit that changes the pin. Coordinate file ownership with an active library maintainer before editing; never reset their work or point qualification builds at a changing sibling checkout. Publishing library commits and changing an application's dependency pin are separate actions.

## Application-side performance findings

The preliminary 500-session/128-exchange run passed checkpoint and full SDK verification, but spent 283.23 seconds around a 10.83-second measured write interval. Probe CPU was 9.54 seconds over the full process lifetime and 2.13 seconds during measurement. The qualification driver retained already-verified probes in the polling loop and queued operations could use the pre-checkpoint goal. Both paths are corrected in `55d6978`. The [paired rerun](../benchmarks/fleet-checkpoint-comparison.json) preserves the same fleet settings and reduces checkpoint plus SDK verification from 272.01 to 32.60 seconds. Full native lifetime changes from 283.23 to 73.58 seconds, CPU from 9.54 to 8.24 seconds, and peak RSS from 60.61 to 67.02 MB. Closed-loop write totals and background contention differ, so this is a checkpoint scheduling observation rather than an ingestion-throughput comparison. This is a benchmark scheduling issue, not evidence of a library or production-daemon performance defect.


The library maintainer's [C4 handoff](LIBRARY_BOUNDARIES.md) records later consensus pipeline, follower batching and control-priority work (`6af73ae`, `7e23145`, `24d5399`, `906383c`). The 27-library compatibility snapshot above predates those commits. Re-capture a coherent revision set before adoption, and wait for the documented DTC2 frame/tag boundary to settle; a passing build of `d7192f7` does not qualify the later C4 revisions.

The subsequent handoff adds snapshot windows (`fcc18e6`) and append payload caps (`c752657`). Defaults preserve the prior one-chunk snapshot window and RPC-sized append cap, but snapshot inactivity timeout behavior changes. Include slow-progress snapshot installation and configured window memory bounds in Pass 3 qualification. The maintainer also records leaf fixes in boolean-parser, time-point, cartesian, md5 and ctrie; inventory actual Detent consumers before adding them to Pass 1. Their library test results do not establish Detent integration results.

## Source findings awaiting the shared benchmark window

The user reopened compilation and load runs on October 16. The following source findings still need focused reproductions and fixes; they do not change the qualification baseline.

| Finding | Evidence | Next action |
| --- | --- | --- |
| Legacy HTTP response allocations have no explicit wire or decoded size ceiling | `src/detent_http/http_client.cc`: header accumulation, `read_all_to_eof`, `reserve(Content-Length)` and gzip/zstd decoding all grow strings without a byte cap. This is the classic blocking client; the managed binary fragment decoder already admits decoded bytes and workspace separately. | Detent integration owns configurable header/wire/decoded bounds with compatible defaults, then oversized Content-Length, EOF-body and compression-expansion regressions. Evaluate the candidate bounded zstd helper during Pass 1; a pin bump alone does not bound existing callers or gzip. |
| Compressor subset configurations skip the current test executable | Candidate `6f6d7c0` registers tests only when all three backends are enabled. Disabling all three also leaves a static target with no sources. | Library maintainer: define the all-disabled configuration contract, then qualify each supported backend subset. Confirm generation behavior after the compilation hold before claiming a reproduced failure. |
| Thread wrapper restart semantics need explicit qualification | Candidate `fdc0a1e` changes join settlement, but `Thread::run()` still gates on `_running`; it does not recreate the one-shot promise/future after completion. README calls `run()` idempotent while running and does not specify post-join restart. | Library maintainer: reproduce run → join → run and concurrent completion/start boundaries under sanitizers, then define and implement restart or one-shot behavior without silently breaking consumers. No new runtime failure is claimed from this source inspection. |
| HTTP offload refusal bypasses application protocol errors | Pinned `http_asio.h` emits plaintext HTTP 503 without `Content-Type` when its offload gate is full. Delivery clients require bounded JSON controls. This is a source-confirmed defect matching the earlier checkpoint error; the diagnostic rerun did not reproduce that error. | Relay integration sizes each reactor gate to the existing global connection cap. Sequential request handling holds at most one offload permit per connection, making that redundant refusal unreachable while preserving application admission bounds. A reusable library overload-response hook remains a separate improvement. |
