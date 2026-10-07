# Validated Architectural Blueprint: resumable reads and terminal retirement

**Date:** 2026-10-06. **Verdict:** Source GO; production acceptance remains conditional. The Lead Software Architect performed a read-only consolidated review using `gpt-6.1-sol` at reasoning effort `max`, after the prior Architect hit its usage limit. The reviewer did not run builds or modify implementation files; qualification results are reported by the implementation agent and checked against the source schedules.

## Scalability Check

One frozen read capture spans exact counting, quota reservation and incremental encoding. Completed quorum reads retain their task and verified boundary without additional captures. Node application, publisher encoding, completion delivery, expiry and terminal retirement each have explicit continuation budgets. Read scheduling occupancy decreases in the measured workloads while total owner CPU and reply wall time increase; no efficiency or fleet capacity claim follows. See [measurements and scope](AUTHORITY_READINESS.md).

## Edge-Case Analysis

Accepted source IO exposes its authentic typed result before deciding whether its consumer remains usable. Peer disconnect and terminal close suppress obsolete verification continuation. Terminal validation and activation callbacks cannot resume normal work or taint successful publication. Accepted initialization, preparation, verification, publication and reclamation retain their original ownership and accounting until settlement; published images may leave activation to recovery.

Read tasks retain admission through bounded key-list disposal. Terminal maps and deques remove their entries during bounded callback slices, avoiding accumulated Task destruction at coroutine exit. The shutdown regression holds sixteen 128-key reads behind reply pressure and verifies terminal responses and eventual admission release.

## Dependency & State Audit

Core owns protocol state; Worker owns accepted storage operations and capabilities; Node owns application roots and arena retirement; Service and Peers retain Node through original settlement. HTTP egress independently owns its counted reply lease. Snapshot replacement retains old read arenas until explicit collection. These strict consensus APIs remain additive and do not migrate Xapiand's legacy Raft mode or change persisted journal formats.

Logical State admission does not independently cap deferred physical reclamation debt. Sustained mutation and producer-session churn must demonstrate bounded debt, RSS and queue age. Monotone growth requires reclamation scheduling or an admission watermark before production acceptance.

## Security & Scale Review

Exact reply counting, ingress retention and original egress settlement bound the principal frontend ownership paths. Cancellation cannot retract a committed write or manufacture an installation acknowledgement. Complete acceptance still requires combined authenticated transport pressure, slow HTTP readers, checkpoint/source overlap and shutdown, plus whole-service telemetry measurements. Isolated codec and memory-retirement benchmarks do not qualify complete Service shutdown or maximum legal callback timing.

## Next component: original Stage 4

1. Define separate versioned distribution snapshots and deltas carrying cluster, restore epoch, authority group, canonical subscription identity, required capabilities and state revision. Deltas bind exact base and target revisions, preserve transaction boundaries, and define deletion, progress watermarks and explicit resynchronization.
2. Export subscriber definitions without authority sessions, ballots or recovery-only tables. A snapshot pins revision R; subsequent deltas begin strictly after R. Keep this wire schema separate from private checkpoints and local mapping layout.
3. Compare Protobuf without gRPC, a narrowly specified MessagePack schema and JSON with zstd on the same corpus. Measure bytes, encode/decode CPU, peak memory, runtime footprint and cross-language support. Enforce framing, decompression, strings, records, overrides and aggregate limits before publication.
4. Build dynamically sized immutable generations away from application reads. Atomically select a complete descriptor or root; never overwrite or truncate a published mapping. Preserve the previous root on failure and distinguish runtime publication from durable restart checkpoints.
5. Add a scoped pinned read view. Acquire all required mappings before exposing the view, bound open/reclamation retries and preserve the previous usable view. Existing get/eval calls may acquire one current view each; consistent multi-read callers retain a scope. Preserve hashing, types, overrides, baked defaults and deletion fallback.
6. Track received, validated, locally published and durably checkpointed cursors separately. Replace the current post-acceptance on_update hook as the recoverable publication boundary; retain legacy JSON polling through an explicit adapter.
7. Keep opaque image publication, mapping lifetime, root selection and reclamation independent of Detent definitions and consensus. Bound candidate builds, abandoned generations and retained ownership, and document disk retained by long-lived views. Stage 4 may build complete generations; Stage 5 adds indexed pages, path copying, streams, subscriptions and relay fan-out.
8. Accept the complete authority-to-codec-to-agent-to-view component with cross-version and malformed-input tests, publication kill points, concurrent old/new readers, reopening and supported-platform memory/concurrency checks. Measure large states, overrides, deletions, peak construction memory, lookup CPU and publication failures. Whole-generation results do not establish sustained update capacity.

Implementation proceeds under the standing authorization. This consolidated source GO permits landing the component and starting Stage 4 while the remaining readiness evidence stays explicitly pending; individual fixes and commits do not require another Architect approval cycle.
