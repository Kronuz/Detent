# Structural insertion: validated architectural blueprint

**Date:** 2026-10-22. **Status:** On hold pending the [model-container comparison](CONTAINER_SELECTION_ARCHITECTURE.md). Ownership qualification does not automatically authorize starting another mutation engine.

## Decision and scalability

Prepare the complete selected insertion path, then commit without failure. Keep the current split bias, sibling selection, public API and node layout. The remaining hazards include counts published before split transfers, destructive root promotion and fallible separator or incoming construction after topology changes.

Use one planner for selected path nodes, siblings, counts, insertion positions and fixed range mappings. Each range identifies original slots or the prepared incoming value. Work and temporary storage remain O(node capacity × selected path height). Preserve the accepted nonfull insertion route; add no per-entry registry, general mutation images, per-tree cache or dependency.

## Preparation and edge cases

Native head/tail split bias can leave an output temporarily empty until insertion finishes. Prepare the complete cascade, separator insertion and incoming placement before consuming original values. A separately published “make room” step is insufficient if subsequent preparation can throw.

Materialize incoming arguments through the accepted forwarding-selection helper before relocation. Preserve duplicate `try_emplace`, hints, contained lvalue aliases, explicit moved arguments, mapped-reference fallback and custom conversions. Preserve duplicate order and resolve the returned iterator from the prepared incoming destination. Resolve child pointers before overwriting reused arrays. Treat the root's parent field as its leftmost-leaf sentinel.

## Dependency and state audit

One topology recipe supports two materialization policies. For qualified nonthrowing key/mapped transfers, prepare fallible key copies in typed storage, gather only relocated mapped values and construct each actual `pair<const Key,T>` at its final destination. Unchanged slots remain live. Scalar keys with nonthrowing copies need no key bank. Ownership moves do not make const-string-key pair movement nonthrowing.

The generic policy builds guarded replacements only for affected nodes, using the same recipes. Copyable values preserve originals. Throwing move-only mapped values may remain moved-from while original keys, counts and topology stay valid. A consuming throwing move-only set-key failure cleans staged storage before canonically clearing the original tree.

Guards own constructed prefixes and exact allocations, never borrowed child subtrees. Prepare allocator conversions, allocations and parent patches before consumption. Commit uses proven nonthrowing construction/destruction and pointer/count updates, publishing size once after the inserted value exists. Insertion stops reaching const-key swap helpers; remaining erase callers stay open.

Inventory noncopyable map-key callers. A const-key pair cannot be legally relocated merely because the former const-cast route compiled. Document this compatibility limit without a blanket container constraint or mutable-pair aliasing.

## Security, cost and qualification

Derive scratch bounds from actual capacities and selected operations, including alignment and child pointers. Use bounded automatic storage where it fits; a cascade may use one exact-sized allocator-rebound block. Finish every fallible allocation, copy, comparison and construction before fast-path consumption. Reuse complete-object lifetime and allocation-extent helpers, globally qualified placement construction and constructed-prefix guards.

1. Attribute positioned failures on the ownership-qualified baseline: root/sibling allocation, separator construction, parent suffix transfer, internal-root promotion and both rebalance directions. Check identities, allocation ownership, counts, ordering and destruction after each refusal.
2. Pass the early cost gate against that same baseline before full matrices. Measure scalar, short/maximum-length keys, nested definitions and generic throwing values, including structural-operation frequency, complete string heaps, scratch, retained/peak bytes and CPU. Ownership gains cannot mask structural overhead.
3. Qualify a frozen component across all four families, comparator orders, duplicates, insertion positions, sibling destinations, multilevel cascades, alignment and stateful/nondefault allocators. Deny ordinary/aligned allocation from the first consuming transfer and require completion.
4. Run macOS/Linux/FreeBSD native, sanitizer and mixed profiles, actual maximum-key definition refusals, unsuppressed publication leak checks and the complete immutable-pin consumer rebuild.

Completion covers full-node insertion, split, promotion and insertion-driven rebalance with accepted costs and documented generic guarantees. Erase/merge const-key mutation and bounded owning drain remain separate release conditions.
