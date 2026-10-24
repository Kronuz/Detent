# Release blocker handoffs

**Date:** 2026-10-23. Detent is in Stage 6, release qualification. Each linked brief is self-contained: it includes priority, repository links, source paths, dependencies and release-unblocking criteria. Share the individual file; this index is optional. Generic library contracts remain independent of application semantics.

| Ask (priority order) | Owner boundary | Completion evidence |
| --- | --- | --- |
| P0, rank 1: [Container safety and selection](container-safety.md) | SC-5, four public model fields and their cleanup | Actual-type failure tests, complete-path costs, explicit representation decision |
| P1, rank 2: [Segment storage scaling](segment-storage.md) | SC-2/SC-3, agent and relay local storage | Fixed-cardinality fragmentation trials, bounded work, unchanged integrity and recovery |
| P1, rank 3: [Ingestion wait attribution](ingestion-waits.md) | SC-4, authority and completion diagnostics | Seed-boundary phase timings that distinguish syscall time from queue/reaping delay |
| P1, rank 4: [Resource primitives](resource-primitives.md) | Existing shared resource adoption | One accounting/retirement engine; domain settlement policy preserved |
| P2, rank 5: [HTTP clients](http-clients.md) | Existing HTTP/TLS library extension and adoption | Unified parser and transport lifecycle; application semantics local |
| P2, rank 6: [Bounded MessagePack](bounded-codec.md) | Existing codec extension and adoption | One lexer and packer; exact payload bytes and bounded-only target |
| P2, rank 7: [Persistent ordered map](persistent-map.md) | Planned reusable-library extraction | Qualified version ownership, allocation accounting, bounded collection and consumer adoption |

The lead owns SC-1 relay lifecycle. Its integrated component passes 219 candidate product tests, the delayed TLS bootstrap regression and scoped ASan/UBSan. Sustained-load capacity remains unqualified. Do not modify its relay session/cache files in the handoff tasks.

Each task starts with a short source/evidence check and a concrete reproduction. Work serially within each task; do not spawn a review tree. Ask for one narrow component review when implementation is complete. Independent tasks may use separate worktrees, but all builds and measurements must coordinate through `quiet` on the shared host. Storage and ingestion agents must agree on any overlapping telemetry or shared-library edits before changing them.

These asks close specific blockers. They do not replace the remaining release gates: capacity trials at 1M, 5M and exploratory 10M, recovery/restore and sustained-load trials, platform qualification, deployment/packaging, dependency adoption and reusable-library boundary checks. See [the release plan](../RELEASE_PLAN.md) and [active status](../status/README.md).

The [extraction checklist](../EXTRACTION_CHECKLIST.md) is the clean-session restart point for library work. It records existing candidates, adoption gates and conditional keep-local decisions; the briefs alone do not imply a new library must be created.

## Local source and evidence mapping

1. **Segment storage:** SC-2/3 in `docs/reviews/scaling-2026-10-23.md`. Start with `src/detent_local/{paged_dependency_discovery,owned_segment_inputs,sparse_plan_finalizer,paged_warm_validator,warm_registry_merge,paged_publication_artifacts,generation,paged_graph_validator,paged_trie_overlay}.cc` and `src/detent_delivery/coordinator.cc`.
2. **Containers:** SC-5, `docs/BTREE_RELEASE_RISK.md`, `docs/CONTAINER_SELECTION_ARCHITECTURE.md`, `benchmarks/model-container-comparison-20261022.json` and `benchmarks/btree-transaction-cost-20261021.json`. Four fields: `Snapshot::knobs`, `SnapshotPatch::knobs`, `BoolDef::overrides`, `OverrideTable::values`. Recorded production pin: `cbc35762bc4a98ad91361be29d05f0ae715a1339`; verify frozen source overrides separately.
3. **Ingestion:** SC-4, `docs/perf/seeding-waits.md`, `docs/status/seeding-waits.md` and `benchmarks/system-capacity-20261023.json`. Baseline `0c1e5ad`: 100K seed 104.338 s/784 transactions; 130K 138.714 s/1,016 transactions. Four producers, batches up to 128. The 10K follow-up takes 10.363 s with 80/80 transactions verified. Its sampled sync occupancy is 5.963 s and misses the final receipt. Start with `tests/detent_system/{authority,writers,mixed}.py` and the existing completion timing hooks.
4. **Persistent map:** `src/detent_authority/persistent_map.hh`; extraction is already listed in `docs/LIBRARY_BOUNDARIES.md`. The candidate uses `new Node` plus a separate shared-pointer control allocation, default-constructed comparators, a single-owner arena and weak arena references in node deleters. The catalog and GitHub repository inventory checked on 2026-10-23 contain no dedicated persistent ordered-map library. These are extraction audit inputs, not independently reproduced defects. Coordinate with the ingestion task before modifying authority allocation or reclamation behavior.

The local checkout is `/Users/kronuz/code/Detent`. Follow its `AGENTS.md` and `docs/SHARED_LIBRARY_DECISIONS.md`; shared libraries live under `~/code/Kronuz/cpp-libraries`. Run `quiet check` before builds/tests; use `quiet light` with at most four jobs and `quiet exclusive` for published comparisons. Keep artifacts under `.scratch/`. Coordinate shared-library edits and pin changes with the lead.
