# Release blocker handoffs

**Date:** 2026-10-23. Detent is in Stage 6, release qualification. The linked briefs are standalone and project-neutral. Share only the individual brief for an anonymous handoff; this index contains the local source and evidence mapping.

| Ask | Owner boundary | Completion evidence |
| --- | --- | --- |
| [Segment storage scaling](segment-storage.md) | SC-2/SC-3, agent and relay local storage | Fixed-cardinality fragmentation trials, bounded work, unchanged integrity and recovery |
| [Container safety and selection](container-safety.md) | SC-5, four public model fields and their cleanup | Actual-type failure tests, complete-path costs, explicit representation decision |
| [Ingestion wait attribution](ingestion-waits.md) | SC-4, authority and completion diagnostics | Seed-boundary phase timings that distinguish syscall time from queue/reaping delay |
| [Persistent ordered map](persistent-map.md) | Planned reusable-library extraction | Qualified version ownership, allocation accounting, bounded collection and consumer adoption |

The lead retains SC-1 relay integration. Its isolated candidate passes 219 product tests and the delayed TLS bootstrap regression; sanitizers and integration remain open. Do not modify its relay session/cache files in the handoff tasks.

Each task starts with a short source/evidence check and a concrete reproduction. Work serially within each task; do not spawn a review tree. Ask for one narrow component review when implementation is complete. Independent tasks may use separate worktrees, but all builds and measurements must coordinate through `quiet` on the shared host. Storage and ingestion agents must agree on any overlapping telemetry or shared-library edits before changing them.

These asks close specific blockers. They do not replace the remaining release gates: capacity trials at 1M, 5M and exploratory 10M, recovery/restore and sustained-load trials, platform qualification, deployment/packaging, dependency adoption and reusable-library boundary checks. See [the release plan](../RELEASE_PLAN.md) and [active status](../status/README.md).

## Local source and evidence mapping

1. **Segment storage:** SC-2/3 in `docs/reviews/scaling-2026-10-23.md`. Start with `src/detent_local/{paged_dependency_discovery,owned_segment_inputs,sparse_plan_finalizer,paged_warm_validator,warm_registry_merge,paged_publication_artifacts,generation,paged_graph_validator,paged_trie_overlay}.cc` and `src/detent_delivery/coordinator.cc`.
2. **Containers:** SC-5, `docs/BTREE_RELEASE_RISK.md`, `docs/CONTAINER_SELECTION_ARCHITECTURE.md`, `benchmarks/model-container-comparison-20261022.json` and `benchmarks/btree-transaction-cost-20261021.json`. Four fields: `Snapshot::knobs`, `SnapshotPatch::knobs`, `BoolDef::overrides`, `OverrideTable::values`. Recorded production pin: `cbc35762bc4a98ad91361be29d05f0ae715a1339`; verify frozen source overrides separately.
3. **Ingestion:** SC-4, `docs/perf/seeding-waits.md`, `docs/status/seeding-waits.md` and `benchmarks/system-capacity-20261023.json`. Baseline `0c1e5ad`: 100K seed 104.338 s/784 transactions; 130K 138.714 s/1,016 transactions. Four producers, batches up to 128. The 10K follow-up takes 10.363 s with 80/80 transactions verified. Its sampled sync occupancy is 5.963 s and misses the final receipt. Start with `tests/detent_system/{authority,writers,mixed}.py` and the existing completion timing hooks.
4. **Persistent map:** `src/detent_authority/persistent_map.hh`; extraction is already listed in `docs/LIBRARY_BOUNDARIES.md`. The candidate uses `new Node` plus a separate shared-pointer control allocation, default-constructed comparators, a single-owner arena and weak arena references in node deleters. The catalog and GitHub repository inventory checked on 2026-10-23 contain no dedicated persistent ordered-map library. These are extraction audit inputs, not independently reproduced defects. Coordinate with the ingestion task before modifying authority allocation or reclamation behavior.

The local checkout is `/Users/kronuz/code/Detent`. Follow its `AGENTS.md` and `docs/SHARED_LIBRARY_DECISIONS.md`; shared libraries live under `~/code/Kronuz/cpp-libraries`. Run `quiet check` before builds/tests; use `quiet light` with at most four jobs and `quiet exclusive` for published comparisons. Keep artifacts under `.scratch/`. Coordinate shared-library edits and pin changes with the lead.
