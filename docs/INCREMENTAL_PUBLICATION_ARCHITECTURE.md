# Immutable incremental publication

**Date:** 2026-10-11
**Status:** Validated Architectural Blueprint, Stage 5C. The Lead Software Architect reviewed scalability, edge cases, dependencies/state, and security/scale at max reasoning effort. Design GO covers this complete component after Stage 5B qualification closes, followed by one consolidated component review. Routine implementation commits do not require architectural approval.

## Boundary

Retain the selected-descriptor and public reader interfaces, profile-1 distribution encoders, publication sink and completion scheduler. Add a sealed paged-publication plan to the existing DiskStore state machine. Whole-image v1 remains supported; paged publication is an explicit configuration choice. Stage 6 retains sustained ingestion, fleet/bootstrap/reconnect storms, multi-host failure domains, audit/rollback and pinned dependency reproduction.

```text
Validated update + exact recovered base
                 ↓
Budgeted page builder → sealed publication plan
                 ↓
Existing DiskStore operations + CompletionPort
                 ↓
Durable root selection → validated recovered root
                 ↓
SDK views + publication sink + profile-1 export
```

## Format and logical identity

Add pure local codecs for immutable data/index pages, bounded segment packs and a small root manifest. Initial qualification uses approximately 64 KiB pages in segments of at most 1 MiB. Definitions exceeding a page use checked overflow references; there is no file per knob.

References bind segment identity, extent and checksum. Before allocating or mapping, bound depth, ranges, counts, aggregate extents and arithmetic. A canonical logical commitment covers definitions, key revisions and namespace watermarks independently of physical packing or update history. Physical checksums bind exact artifacts. Same-cursor equality cannot depend on page placement or compaction history.

## Builder and publication plan

Accept existing validated snapshots/deltas and an exact recovered base. Normalize repeated mutations with established transaction-order rules; replace affected leaves and index paths while retaining unchanged immutable subtrees. A move-only sealed plan owns new segment bytes, root descriptor, logical commitment, exact base descriptor/directory binding and validated reuse capabilities.

Gathering, sorting, hashing, zeroing and disposal obey work budgets. Progress without scoped state changes replaces root metadata only. Full snapshots and explicit migration may traverse the complete state.

## Durable selection

Add a plan overload to DiskStore publication while retaining ImageBuilder callers. Compose existing managed preparation/publication operations and CompletionPort; add neither a second IO engine nor a journal rewrite.

Reserve peak coexistence of new segments, ownership records and root selection before creation. Durable ownership precedes large payload writes. Synchronize every dependency before root selection and its directory barrier. Cancellation after possible selection requires authentic reconciliation before visibility or upstream feedback.

## Recovery and readers

Give the private ReadView root a format-specific implementation, preserving public lookup/evaluation behavior and dependencies. Cold recovery validates the complete reachable graph. Warm recovery validates new pages and reuses previously validated immutable subtree capabilities bound to exact storage, directory and file identities plus checksums. Neither a descriptor nor a cursor alone authorizes reuse.

Resolve required mappings and references before installing a root; hot lookup performs no filesystem work. Root leases remain separate from shared segment mappings. Reusing bytes cannot retain every obsolete ancestor generation indefinitely.

## Reclamation and compaction

Extend existing census, ownership proofs and reclamation operations for named segments and graph metadata. Protect selected, staged, uncertain and reader-leased roots. A segment's birth generation or a retired root's age cannot authorize reclamation of a dependency reachable from another protected root.

Use admitted traversal/mark capacity, bounded background work and selection rechecks before deletion. Preserve corrupt, foreign or unprovable artifacts and retain complete over-quota debt. Refund only after proved deletion, required directory barriers and handle closure.

Bounded compaction replaces sparse retained segment packs under the same admission and selection rules. Pinned readers retain old resources. Same-cursor compaction is local maintenance with verified logical equality; it creates neither an upstream feedback window nor a wire update.

## Compatibility and migration

Keep v1 codecs and publication callable. Upgrade readers before enabling v2 writers. Reuse the selected-descriptor shape if it unambiguously binds a versioned root manifest; version records only where their semantics require it.

An explicit migration capability binds the actual selected descriptor and directory, preserves Context/profile and proves same-cursor logical equality before selection. Existing readers retain their installed view on rejection. Cold recovery never substitutes an older generation for a corrupt selected root.

Coordinator configuration chooses the builder/plan while preserving recovered-head reconciliation and durable feedback. Adapt pinned-image traversal and profile-1 count/emission encoders to paged views. Keep physical-format identity separate from wire semantic identity. Maintenance refreshes recovered physical binding without authorizing a scope/policy transition or fabricating an update edge. Preserve sibling source APIs; document required consumer rebuilds.

## Resource and measurement contract

Charge changed-page buffers, indexes, validation capabilities, mapping controls and retirement debt to existing account classes. Bound total reachable state, mappings, segment/page counts and traversal workspace; a small root manifest cannot bypass state limits.

Ordinary sparse publication avoids both full-image rewriting and a full warm semantic scan. Record new/reused pages, written bytes, validation work, mappings, named disk debt, compaction work and pinned debt. Total measurements include background reclamation.

## Complete-component acceptance

1. Compare v1/v2 complete key sets, revisions, rollout behavior, repeated mutations, deletion/reinsertion, progress, empty scopes and default fallback. Logical commitments agree across snapshot builds, different update histories and compaction.
2. Inject failures and process kills around dependency writes/syncs, selection rename and directory sync. Recovery yields a valid old or new selection; ambiguous selection fences. Cache visibility and feedback never precede required durability.
3. Exercise shared segments across current and pinned retired roots, cross-process readers, late lease acquisition, saturation, cancellation and facade loss. One-unit-short admission preserves selection; final release conserves physical charges.
4. Reject malformed extents, overflow, cycles, excessive depth, overlapping ranges, invalid counts and checksum/identity conflicts. Qualify explicit migration and unsupported-format rejection.
5. Run paired 100k/500k workloads with one, ten and 1,000 changed keys, progress-only updates and full snapshots. Record CPU, written bytes, syncs, validation counts, owner lateness, reader latency, mappings, descriptors and disk amplification. Single-key warm work follows affected pages and paths; progress-only work follows root metadata.
6. Qualify authority → relay → agent → separate readers, restart/migration, Linux/FreeBSD, ASan/UBSan, assembled TSan, reader/agent/relay build profiles, legacy regression and relevant sibling builds.
