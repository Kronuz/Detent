# Ask: close Detent's btree safety and container decision

Work in `/Users/kronuz/code/Detent`. Detent is a configuration distribution system nearing first-release qualification. Its managed authority's main key index is a persistent AVL, and application SDK reads use mapped pages. The btree question concerns four model fields: `Snapshot::knobs`, `SnapshotPatch::knobs`, `BoolDef::overrides` and `OverrideTable::values` in `include/detent/model.hh` and `include/detent/overrides.hh`. Bool definitions also enter authority construction and cleanup.

## Evidence and decision boundary

Read `docs/BTREE_RELEASE_RISK.md`, `docs/CONTAINER_SELECTION_ARCHITECTURE.md`, `docs/DEPENDENCY_UPGRADES.md` and SC-5 in `docs/reviews/scaling-2026-10-23.md`. Production's recorded btree pin is `cbc35762bc4a98ad91361be29d05f0ae715a1339`. Verify the actual source override and dependency manifest in each build; a CMake pin alone does not identify a scratch build's headers.

First-insertion and copy repairs are retained. A staged full-mutation approach has unacceptable measured costs in `benchmarks/btree-transaction-cost-20261021.json`. Full-node const-key mutation and failure cleanup remain unclosed for actual Detent types. Upstream publication or passing ordinary trials does not close those risks.

Standard-map and unordered-map prototypes exist; no production representation is selected. `benchmarks/model-container-comparison-20261022.json` contains five alternating 100K pairs, including wins and losses. Hash lookup can improve broad hits while worsening misses and ordered construction. Both standard candidates retain more memory than btree. Standard string hashing is not an accepted policy for adversarial network keys.

## Requested work

1. Inventory the exact actual-type correctness gaps and reproduce each independently. Separate proven defects from hypothetical generic-library guarantees that Detent does not require.
2. Recover the existing comparison work and artifacts before writing another prototype. Complete actual parse, copy, PATCH, publication, partial failure and disposal tests for the viable candidates.
3. Recommend one production representation from complete lifecycle CPU, requested heap and safety evidence. Standard ordered maps are a valid conservative choice if costs fit; repair btree only if a measured constraint justifies more structural work. A hash choice must include collision policy and sorted publication costs.
4. Implement the selected representation in a separate commit after documenting the decision. Preserve one production path. Remove an unused direct btree dependency if appropriate; do not retire the upstream library as part of this task.

## Acceptance

Use actual definitions with short and maximum-length names, three overrides, random/ordered insertion, broad and hot-key reads, misses, final transfer and pruning. Include retained defaults, live snapshots and history when reporting memory. Measure allocation requests separately from RSS. Keep publication sorting scratch and CPU in the hash total.

Require exact JSON/checkpoint/command-digest/shared-memory bytes, evaluation and duplicate behavior, deterministic ordering, denied-allocation cleanup, shared-arena/capture collection and bounded authority override disposal. Unsuppressed leak checking is required where supported; ASan on macOS with leak detection disabled is not a leak qualification. Changing public C++ field types changes layouts and iterators: state that break and rebuild all consumers together.

## Execution and delivery

Follow repository `AGENTS.md` and read shared-library decisions before pin changes. Work in a separate worktree. Coordinate any btree edits with the sibling-library owner; do not modify relay bootstrap or storage ordering work. Run `quiet check` before builds/tests; use `quiet light` with at most four jobs and `quiet exclusive` for published comparisons. Scratch data stays under `.scratch/`.

Work serially, reusing evidence. Return a concrete decision, isolated fix commits, reproducible before/after results, failure-test results and compatibility notes. Do not push or change shared pins without lead coordination. If no candidate meets an explicit budget, name that budget and the measured failing case; do not leave a generic “btree unresolved” report.
