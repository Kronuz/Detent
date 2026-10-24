# Ask: bound segment indexing, publication and compaction work

Review a C++20 immutable paged-storage engine used by a replicated data-distribution service. Readers retain mapped generations while writers publish new ones. Pages live in bounded segment packs; small updates can leave many partly live packs even when record count stays fixed. The task is to remove avoidable work while preserving integrity, crash consistency and reader lifetime.

This brief is independent of any application or repository. Locate the mechanisms below in the supplied source; report which are present before proposing changes. Source-specific evidence belongs in the accompanying local index and is not required to understand the requested behavior.

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
