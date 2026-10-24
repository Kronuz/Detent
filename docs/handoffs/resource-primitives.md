# Ask: adopt shared accounting and deferred-retirement primitives

Review a C++20 service with private byte ledgers, allocation adapters, reservation bookkeeping and deferred object destruction. Suitable shared resource primitives already exist or are under qualification. Recover existing migration work before writing replacements. This brief is independent of any application or repository.

## Scope

1. Reuse one concurrent byte-accounting mechanism with move-only charges, parent admission and rollback, physical-free ordering and owning allocator context.
2. Reuse a separate single-owner reservation mechanism for explicit reserve/grow/cancel/settle/reclaim. Durable disk debt requires proof before refund; automatic memory-charge destruction must not imply durable disk deletion.
3. Reuse caller-owned retirement domains with bounded cleanup turns. Accepted IO retains original ownership until authentic settlement. Domain teardown cannot destroy transport bytes or call a dead owner.
4. Preserve thin domain wrappers where they express policy. Delete their duplicate arithmetic, allocator or retirement engines after migration. No parallel ledger repository or global allocation interception is required.

## Acceptance and delivery

Audit existing APIs and every owner/address lifetime first. Account for control blocks, allocator metadata, retained versions and deferred work. Exercise parent refusal, overflow, allocation failure, unresolved reservations, cross-owner release where supported, cancellation, shutdown and final conservation. Distinguish admitted/requested heap from RSS, mappings and external allocator overhead.

Optional resource targets must not enable unrelated allocator interception or fetch unrelated dependencies. Measure default footprint and sustained retirement costs before/after, including losing cases. Run library and composed consumer tests on supported platforms with relevant sanitizers. Keep valid durable/wire bytes and acknowledgment timing unchanged.

Deliver separate shared-library and consumer-adoption commits, one implementation per mechanism, immutable revisions, removed-private-code inventory and reproducible evidence. Work serially in isolated checkouts and coordinate shared-host builds and measurements. Follow repository instructions and coordinate shared API changes with other consumers. Report unsupported lifetime or accounting semantics explicitly rather than retaining a private fallback.
