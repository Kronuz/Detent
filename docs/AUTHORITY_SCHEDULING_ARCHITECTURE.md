# Authority owner-loop scheduling

**Date:** 2026-10-25. **Status:** Validated Architectural Blueprint; implementation and comparative qualification pending.

Snapshot export must amortize owner-loop overhead while preserving bounded publisher and quorum responsiveness. Keep immutable captures, AVL traversal and retirement on their current owner. Do not move owner-managed roots between threads or introduce a snapshot cache or protocol change in this component.

## Evidence and scope

The 5M counting control reaches 4,705,298 sampled records before its first 60-second expiry. Boolean-heavy and integer-heavy namespaces have different costs, so one interval cannot predict complete-snapshot throughput. A short profiler sample shows completion dispatch and worker activity between small export steps; it does not establish their causal share. The [dependency investigation](DEPENDENCY_UPGRADES.md) records the pinned implementations and observed counter deltas.

Every authority turn calls `Node::service()`, which progresses application, storage and reclamation as well as clock events. Skipping that entire call when milliseconds repeat can strand work. Storage dispatch only spawns operations supplied by the worker; a blanket dispatch throttle can delay persistence or original-operation reaping.

## Component plan

1. Measure owner turns, distinct tick milliseconds, entropy draws, export actions, dispatched storage operations and actual primitives. Distinguish maintenance from accepted persistence. Cache election entropy per distinct tick; retain progress and blocked-timer retries between clock changes.
2. Investigate duplicate clock submissions and empty maintenance against the actual pinned libraries. Extend an existing upstream API if progress must be separated from clock admission. Suppress reclamation only with an authoritative work predicate and a defined periodic fallback. Record shared policy decisions before adoption.
3. Experiment with a bounded local export burst after the normal control/publisher pass. Reuse existing small encoder steps, with both finite byte/action credits and an experimental wall-time cap around 250 microseconds. Check expiry and cancellation between steps. Stop on backpressure, pending frames, completion or no progress. Always yield before another burst and service control/publishers first.

## Review and acceptance

| Check | Required evidence |
| --- | --- |
| Scalability | Compare unchanged baseline, churn reduction and combined burst over the same frozen corpus, limits and record mix. Report complete preflight/emission wall and CPU time and owner turns per million actions. Expired work remains censored. |
| Edge cases | Idle service, tiny budgets, slow receivers, cancellation, disconnect, shutdown and blocked timer retries keep making bounded progress. |
| Dependency and state | Preserve one capture, existing frame/accounting bounds, canonical bytes and owner-local reclamation. Modify shared mechanisms upstream rather than patching fetched copies. |
| Security and scale | Measure publisher/quorum latency under concurrent writes, stable leadership and both relay cursors. Keep maximum session lifetime, authorization and hard request bounds. |

Require an improvement beyond run variation without increased capture/buffer high-water marks. Review the complete component and its evidence once before landing. A larger response allowance alone does not satisfy this performance gate.
