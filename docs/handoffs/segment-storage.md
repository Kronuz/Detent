# Ask: remove avoidable segment storage scaling costs

Work in `/Users/kronuz/code/Detent`. Detent distributes configuration knobs through a replicated authority, relays, and local agents. Applications read immutable mapped pages from agents. Relays and agents share `detent_local` storage. A segment is a pack of pages, at most 1 MiB; updates can leave many partially live packs even at fixed key count.

## Problem and evidence

Independent source review identifies SC-2 and SC-3 in `docs/reviews/scaling-2026-10-23.md`. These are demonstrated complexity paths, not measured explanations of seed time. Seeding runs before relays and agents start.

1. `src/detent_local/sparse_plan_finalizer.cc` copies all segment reachability rows per delta. `paged_warm_validator.cc`, `warm_registry_merge.cc` and `paged_publication_artifacts.cc` also process whole segment directories. `generation.cc` checks ownership and payload names across all reused segments, including warm recovery. A one-key update therefore has costs that grow with segment count.
2. `paged_trie_overlay.cc` visits all pages to compact a selected subset of packs. The coordinator's eight-pack selection bound does not bound that traversal.
3. `paged_dependency_discovery.cc` and `owned_segment_inputs.cc` insert into sorted vectors by moving suffixes, creating worst-case quadratic segment-directory work. `sparse_plan_finalizer.cc` insertion-sorts new packs.
4. Cold discovery hashes segments, then `paged_graph_validator.cc` hashes them again. Removing either pass requires a proof tied to the exact immutable mapping and ownership identity.

## Requested work

1. Reproduce with a deterministic workload holding key count fixed while increasing segment count, fragmentation and held generations. Separately vary key count. Start small; no expensive 5M trial is needed to establish these mechanisms.
2. Record segment rows moved/copied, pages visited, bytes hashed, metadata syscalls, requested heap and peak executor-turn work. Establish the baseline before changing the mechanism.
3. Implement the smallest complete component first: budgeted segment ordering with accounted storage, duplicate rejection and deterministic final order. Use an existing library mechanism where appropriate; inspect `~/code/Kronuz/cpp-libraries/AGENTS.md` before proposing extraction or library changes.
4. Address whole-directory publication/compaction work in a separate component after the ordering result. Propose a bounded mechanism before implementing a structural change. Keep duplicate hashing as a separately reviewed integrity change.

## Constraints and acceptance

Preserve storage and protocol bytes, corruption detection, duplicate rejection, crash recovery, resource refusal, immutable-reader lifetime and incremental retirement. No unbudgeted whole-container sort on the owner executor. Do not remove identity checks merely to reduce syscalls. Prefer library defaults; document measured reasons for overrides. One implementation per mechanism.

Tests must include reverse/random segment order, duplicate identities, tiny work budgets, allocation refusal, interrupted construction, cancellation, cold/warm recovery, compaction with held readers and final resource conservation. Compare complete decoded state and durable bytes before/after. Publish raw repeatable measurements, source/dependency identities and losing cases. A source-level complexity improvement alone does not establish a timing win.

## Execution and delivery

Read repository `AGENTS.md`, `docs/SHARED_LIBRARY_DECISIONS.md`, the SC-2/3 review and relevant storage tests. Use a separate worktree; do not edit the relay bootstrap candidate. Before any build/test/measurement run `quiet check`; wrap builds/tests in `quiet light` with at most four jobs, published comparisons in `quiet exclusive`. Keep scratch data under `.scratch/`.

Return separate fix commits with tests, raw artifact paths, a short before/after table and remaining risks. Do not push or alter shared-library pins without coordinating with the lead. Stop at a concrete design choice if safety requires a format or public API change; provide the exact proposed contract rather than a general blocker.
