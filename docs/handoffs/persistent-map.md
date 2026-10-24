# Ask: extract and qualify a small persistent ordered map

**Priority:** P2 · Rank 7 of 7 · Required generic-library extraction.

Required before release, after immediate safety/scale blockers and resource ownership contracts. Do not confuse this tree with the separate mutable B-tree safety task.

## Repositories and starting point

1. **Start from the existing implementation:** [Kronuz/Detent/blob/main/src/detent_authority/persistent_map.hh](https://github.com/Kronuz/Detent/blob/main/src/detent_authority/persistent_map.hh).
2. **Consumer repository:** [Kronuz/Detent](https://github.com/Kronuz/Detent).
3. **Check existing libraries before creating a repository:** [Kronuz/cpp-libraries](https://github.com/Kronuz/cpp-libraries).
4. **Shared-library rules:** [Kronuz/cpp-libraries/blob/main/AGENTS.md](https://github.com/Kronuz/cpp-libraries/blob/main/AGENTS.md).

**Inspect first:** `src/detent_authority/persistent_map.hh` and its authority callers/tests. The candidate uses standard-library facilities, `new Node`, separate shared-pointer control allocation and a single-owner retirement arena. A catalog/repository check on 2026-10-23 finds no dedicated persistent ordered-map library; recheck before creating proposed `persistent-map`.

**Dependencies and ownership:** Audit may start now; implement against the resource task's final allocation/reclamation contracts. Coordinate authority edits with ingestion diagnostics. New repository creation/public API publication needs owner coordination.

**Release-unblocking outcome:** A qualified generic version/ownership API, one extracted implementation, consumer adoption and deletion of its private engine, historical-version/failure tests and measured allocation/reclamation costs.

This file is the complete task brief; no handoff README or conversation history is required. Repository links identify source and integration fixtures; reusable APIs must remain application-neutral. Read each edited repository's `AGENTS.md`. Existing candidates may be older than main: inspect current APIs and preserve uncommitted work before transplanting them.

On the shared development host, run `quiet check` before builds/tests; use `quiet light` with at most four jobs and `quiet exclusive` for published measurements. Keep scratch artifacts under `.scratch/`. Work serially within this task and use one narrow component review, not a review tree. Deliver isolated commits, exact source/dependency identities, reproducible commands, test results and remaining limits.

Review a C++20 immutable ordered-map implementation based on a path-copying AVL tree. Each update returns a new root and shares unchanged nodes with earlier versions. “Persistent” means old versions remain available in memory; this task includes no disk format, journal or replication.

## Existing mechanism and boundary

The candidate is a template over key, value and comparator, using only the standard library. It provides lookup, functional set/erase, ordered traversal, a root-pinning lower-bound cursor, and explicitly budgeted collection. Nodes use shared ownership and path copying; last-reference release normally queues nodes into a shared arena for later deletion. Root capture shares storage without copying every record.

Ownership changes and collection require one owner executor. Reference-counted nodes alone do not make the complete map and its retirement arena thread-safe. The library must state whether readers can cross threads and precisely which operations require external synchronization.

## Requested work

1. Audit ownership, exception safety and reclamation before extraction. Cover failure after node construction but before ownership-control allocation, destruction with live version/cursor handles, partial updates, and final arena teardown. Distinguish bounded explicit collection from potentially unbounded final destruction and arbitrary key/value destructors.
2. Define a narrow generic API around immutable versions, ordered lookup/traversal and explicit collection. Keep storage, application records, networking, durability and executor implementations outside it. Document comparator requirements; a default-constructed comparator per lookup cannot silently support stateful comparison.
3. Make node and ownership-control allocations support the caller's allocator/accounting policy. Measure the cost before selecting a more complicated ownership mechanism. Prefer defaults and existing resource primitives; add no private parallel ledger.
4. Extract one implementation with no application includes. Replace the consumer's implementation with the library dependency in a separate adoption commit. A compatibility alias may forward to the same implementation; it must not retain another tree engine.

## Acceptance

Use a standard ordered map as the oracle for randomized updates, deletion and lower-bound traversal. Retain multiple historical versions and cursors while mutating newer ones, then verify each version independently. Check balance, height and subtree counts after every operation. Test missing-key erase, replacement, empty roots, copy/move behavior and comparator edge cases.

Exercise allocation failure at every allocation point and throwing key/value construction where supported. Verify unchanged older roots, no escaped nodes, correct accounting and eventual collection. Deny allocation during cursor traversal and explicit collection where the API promises no allocation. Measure collection turns and separately document final teardown limits.

Benchmark increasing key counts, update batch sizes and retained-version counts. Report lookup/update CPU, allocation requests, retained/peak heap and reclamation cost. Structural sharing saves complete copies but retains old nodes; quantify that tradeoff. No concurrency claim without matching tests and sanitizers.

Deliver a small library with documented ownership contract, focused tests, CMake target, usage example, accurate license/provenance, catalog entry and platform/sanitizer CI. Preserve consumer-visible behavior and durable bytes. Return separate extraction and adoption commits with before/after evidence. Coordinate publishing and shared API changes with the owner; do not add unrelated container features.
