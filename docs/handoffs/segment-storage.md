# Ask: bound segment indexing, publication and compaction work

**Priority:** P1 · Rank 2 of 7 · Capacity-qualification blocker.

Complete before larger capacity trials: avoidable segment-directory and compaction costs prevent credible scale qualification.

## Repositories and starting point

1. **Start repository:** [Kronuz/Detent](https://github.com/Kronuz/Detent).
2. **Architecture evidence:** [Kronuz/Detent/blob/main/docs/reviews/scaling-2026-10-23.md](https://github.com/Kronuz/Detent/blob/main/docs/reviews/scaling-2026-10-23.md).
3. **Shared-library rules:** [Kronuz/cpp-libraries/blob/main/AGENTS.md](https://github.com/Kronuz/cpp-libraries/blob/main/AGENTS.md).

**Inspect first:** `src/detent_local/paged_dependency_discovery.cc`, `owned_segment_inputs.cc`, `sparse_plan_finalizer.cc`, `paged_warm_validator.cc`, `warm_registry_merge.cc`, `paged_publication_artifacts.cc`, `generation.cc`, `paged_graph_validator.cc` and `paged_trie_overlay.cc`; compaction selection is in `src/detent_delivery/coordinator.cc`. Read findings SC-2/SC-3.

**Dependencies and ownership:** Can start now in a separate worktree. Own local segment indexing and traversal; coordinate allocator/retirement changes with the resource task. Do not modify relay session/cache semantics.

**Release-unblocking outcome:** First deliver a fixed-cardinality fragmentation reproduction and budgeted ordering fix. Close remaining whole-directory/compaction costs in separately reviewed components, with measured work bounds, integrity tests and unchanged decoded state.

This file is the complete task brief; no handoff README or conversation history is required. Repository links identify source and integration fixtures; reusable APIs must remain application-neutral. Read each edited repository's `AGENTS.md`. Existing candidates may be older than main: inspect current APIs and preserve uncommitted work before transplanting them.

On the shared development host, run `quiet check` before builds/tests; use `quiet light` with at most four jobs and `quiet exclusive` for published measurements. Keep scratch artifacts under `.scratch/`. Work serially within this task and use one narrow component review, not a review tree. Deliver isolated commits, exact source/dependency identities, reproducible commands, test results and remaining limits.

Review a C++20 immutable paged-storage engine used by a replicated data-distribution service. Readers retain mapped generations while writers publish new ones. Pages live in bounded segment packs; small updates can leave many partly live packs even when record count stays fixed. The task is to remove avoidable work while preserving integrity, crash consistency and reader lifetime.

## Mechanisms to investigate

1. Incremental publication copies or validates all segment-directory rows and performs filesystem checks on every reused segment. One-record updates can therefore depend on total segment count.
2. Compaction chooses a bounded number of packs but visits the complete page graph to find their live pages. An output bound does not bound traversal work.
3. Dependency discovery inserts each segment into a sorted vector by shifting its suffix. Random or reverse discovery order can produce quadratic moves. Candidate collection and final ordering can repeat this pattern.
4. Cold discovery hashes segment bytes, and subsequent graph validation hashes the same bytes again. Eliminating a pass requires a proof tied to the exact immutable mapping and ownership identity.

These are hypotheses to verify against the supplied implementation. Complexity analysis does not establish their timing contribution.

## Requested work

1. Establish a deterministic baseline holding record count fixed while varying segment count, fragmentation and retained generations. Separately vary record count. Measure directory rows moved/copied, pages visited, bytes hashed, metadata syscalls, requested heap and maximum executor-turn work.
2. Complete the ordering component first: one accounted index while collecting, followed by deterministic ordered emission. Preserve duplicate rejection. Use budgeted O(S log S) ordering rather than repeated vector insertion; S is segment count. An existing suitable mechanism takes precedence over a parallel implementation.
3. Treat publication metadata and compaction as a separate component. Propose how unchanged directory structure can be shared and selected packs located without complete graph traversal. State the retained-memory and recovery tradeoffs before implementation.
4. Treat repeated hashing as an independent integrity change. Specify the internal validation proof and its invalidation rules before removing checks.

## Constraints and acceptance

Preserve durable and protocol bytes, corruption detection, duplicate rejection, resource refusal, crash recovery, immutable-reader lifetime and incremental retirement. No whole-container sort or cleanup hidden inside one owner-executor turn. Do not remove ownership or identity checks merely to reduce syscalls. Hash indexes need an adversarial-input policy; an ordered index avoids that extra policy.

Tests cover reverse/random order, duplicates, tiny work budgets, allocation refusal, interrupted construction, cancellation, cold/warm recovery, compaction with held readers and final resource conservation. Compare complete decoded state and persisted bytes. Published measurements include repeated before/after runs, source/dependency identities, raw artifacts and losing cases. State when a complexity improvement has no measured latency benefit.

Work serially in an isolated checkout and coordinate builds with the host's load-control mechanism. Follow local repository and shared-library rules. Return a bounded design, separate implementation commits per component, regression tests, a before/after table and remaining limits. If a required mechanism is absent, report that finding rather than adding speculative infrastructure.
