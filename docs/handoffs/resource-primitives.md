# Ask: adopt shared accounting and deferred-retirement primitives

**Priority:** P1 · Rank 4 of 7 · Extraction foundation.

Required before release and a prerequisite for clean HTTP/persistent-map adoption. Shared primitives exist; finish consumer migration rather than inventing another engine.

## Repositories and starting point

1. **Start repository:** [Kronuz/allocators](https://github.com/Kronuz/allocators).
2. **Consumer migration and tests:** [Kronuz/Detent](https://github.com/Kronuz/Detent).
3. **Existing blueprint:** [Kronuz/Detent/blob/main/docs/RESOURCE_EXTRACTION_ARCHITECTURE.md](https://github.com/Kronuz/Detent/blob/main/docs/RESOURCE_EXTRACTION_ARCHITECTURE.md).
4. **Shared-library rules:** [Kronuz/cpp-libraries/blob/main/AGENTS.md](https://github.com/Kronuz/cpp-libraries/blob/main/AGENTS.md).

**Inspect first:** Shared `allocators::resources` and `allocators::retirement`; consumer `include/detent_distribution/{storage,allocator,memory_resource,retirement}.hh`, `src/detent_distribution/retirement.cc` and `include/detent_local/disk_ledger.hh`. Existing evidence is in `benchmarks/resource-extraction-qualification-20261019.json` and `benchmarks/resource-extraction-ownership-20261019.json`.

**Existing local candidate:** `.scratch/library-adoption-20261017/detent-resource-migration` relative to the consumer checkout, if available. Inspect both tracked and uncommitted work. A fresh clone will not contain this scratch worktree; use the linked committed blueprint and evidence to recover the migration, and do not assume it is already on main.

**Dependencies and ownership:** Can start now. It is the first extraction to adopt. Coordinate overlapping ownership/accounting edits with storage, HTTP, ingestion and persistent-map tasks. Do not change their domain policies.

**Release-unblocking outcome:** One shared accounting/retirement implementation adopted by the consumer, private engines removed, footprint measured and composed failure/lifetime/platform checks passing. Library publication alone is insufficient.

This file is the complete task brief; no handoff README or conversation history is required. Repository links identify source and integration fixtures; reusable APIs must remain application-neutral. Read each edited repository's `AGENTS.md`. Existing candidates may be older than main: inspect current APIs and preserve uncommitted work before transplanting them.

On the shared development host, run `quiet check` before builds/tests; use `quiet light` with at most four jobs and `quiet exclusive` for published measurements. Keep scratch artifacts under `.scratch/`. Work serially within this task and use one narrow component review, not a review tree. Deliver isolated commits, exact source/dependency identities, reproducible commands, test results and remaining limits.

Review a C++20 service with private byte ledgers, allocation adapters, reservation bookkeeping and deferred object destruction. Suitable shared resource primitives already exist or are under qualification. Recover existing migration work before writing replacements.

## Scope

1. Reuse one concurrent byte-accounting mechanism with move-only charges, parent admission and rollback, physical-free ordering and owning allocator context.
2. Reuse a separate single-owner reservation mechanism for explicit reserve/grow/cancel/settle/reclaim. Durable disk debt requires proof before refund; automatic memory-charge destruction must not imply durable disk deletion.
3. Reuse caller-owned retirement domains with bounded cleanup turns. Accepted IO retains original ownership until authentic settlement. Domain teardown cannot destroy transport bytes or call a dead owner.
4. Preserve thin domain wrappers where they express policy. Delete their duplicate arithmetic, allocator or retirement engines after migration. No parallel ledger repository or global allocation interception is required.

## Acceptance and delivery

Audit existing APIs and every owner/address lifetime first. Account for control blocks, allocator metadata, retained versions and deferred work. Exercise parent refusal, overflow, allocation failure, unresolved reservations, cross-owner release where supported, cancellation, shutdown and final conservation. Distinguish admitted/requested heap from RSS, mappings and external allocator overhead.

Optional resource targets must not enable unrelated allocator interception or fetch unrelated dependencies. Measure default footprint and sustained retirement costs before/after, including losing cases. Run library and composed consumer tests on supported platforms with relevant sanitizers. Keep valid durable/wire bytes and acknowledgment timing unchanged.

Deliver separate shared-library and consumer-adoption commits, one implementation per mechanism, immutable revisions, removed-private-code inventory and reproducible evidence. Work serially in isolated checkouts and coordinate shared-host builds and measurements. Follow repository instructions and coordinate shared API changes with other consumers. Report unsupported lifetime or accounting semantics explicitly rather than retaining a private fallback.
