# Release blocker handoffs

**Date:** 2026-10-23. Detent is in Stage 6, release qualification. These tasks require no conversation history. Give an agent the complete linked file, or its path with access to this checkout.

| Ask | Owner boundary | Completion evidence |
| --- | --- | --- |
| [Segment storage scaling](segment-storage.md) | SC-2/SC-3, agent and relay local storage | Fixed-cardinality fragmentation trials, bounded work, unchanged integrity and recovery |
| [Container safety and selection](container-safety.md) | SC-5, four public model fields and their cleanup | Actual-type failure tests, complete-path costs, explicit representation decision |
| [Ingestion wait attribution](ingestion-waits.md) | SC-4, authority and completion diagnostics | Seed-boundary phase timings that distinguish syscall time from queue/reaping delay |

The lead retains SC-1 relay integration. Its isolated candidate passes 219 product tests and the delayed TLS bootstrap regression; sanitizers and integration remain open. Do not modify its relay session/cache files in the handoff tasks.

Each task starts with a short source/evidence check and a concrete reproduction. Work serially within each task; do not spawn a review tree. Ask for one narrow component review when implementation is complete. Independent tasks may use separate worktrees, but all builds and measurements must coordinate through `quiet` on the shared host. Storage and ingestion agents must agree on any overlapping telemetry or shared-library edits before changing them.

These asks close specific blockers. They do not replace the remaining release gates: capacity trials at 1M, 5M and exploratory 10M, recovery/restore and sustained-load trials, platform qualification, deployment/packaging, dependency adoption and reusable-library boundary checks. See [the release plan](../RELEASE_PLAN.md) and [active status](../status/README.md).
