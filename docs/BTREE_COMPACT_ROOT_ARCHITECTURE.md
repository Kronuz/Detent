# Guarded compact-root growth: validated architectural blueprint

**Date:** 2026-10-22. **Status:** Bounded implementation qualified across three platforms and the publication fault suite; production adoption pending.

## Abstract

Replace only compact leaf-root growth with guarded construction of the final replacement. The actual-type allocation probe leaves a replacement unowned when a later nested copy throws. This component keeps the old root published until every replacement value exists. It introduces no planner, images or scratch allocations.

## Scope and alternatives

1. **Keep native root swapping.** Rejected: the attributed nested-copy refusal strands its allocated replacement. Catching the exception without tracking constructed values is insufficient.
2. **Adopt the general transaction draft.** Rejected at its measured mutation cost. Its stronger generic guarantees are unnecessary for this bounded correction.
3. **Guard the replacement leaf directly.** Selected: preserve geometric capacities and construct actual values in their final slots under prefix ownership. Other insertion and erase paths remain unchanged.

## Scalability

Allocate the same next geometric-capacity leaf as the current branch. Work is O(current root capacity), bounded by `kNodeValues`. Return the inserted iterator directly after publication, bypassing later native shifting. The helper allocates no temporary containers or planning storage.

Use whole-value `move_if_noexcept` initially. A const string key can make pair movement potentially throwing, so nested mapped values may be copied. Measure that cost against the preceding published revision before considering a more specialized policy.

## Construction and failure ownership

Construct incoming arguments first, directly at final insertion position. This protects contained lvalue aliases such as `try_emplace(new_key, begin()->second)` from being consumed during original-value relocation. Track the initially disjoint incoming slot separately from the contiguous constructed prefix.

Construct originals around that slot using `move_if_noexcept`. Advance prefix count only after successful construction. When iteration reaches the incoming position, adopt that existing value into the prefix and clear its separate ownership flag. On failure, destroy any still-disjoint incoming value, then destroy the counted prefix and free the replacement with the existing allocator helpers.

Keep the old root published throughout preparation. Copyable values retain contents when copied; noexcept moves cannot subsequently fail. Throwing move-only mapped values may remain moved-from while keys, lifetimes and topology stay valid. For noncopyable potentially throwing set keys, mark consumption before attempting an original-key move; if it throws, clean the replacement and canonically clear the old tree. Explicit moves from contained mapped arguments can consume them; moving contained set keys is outside the supported aliasing contract.

## Dependency and state boundary

Preserve public signatures, node layout, geometric capacities and allocator ownership. Construct actual const-key pairs without swaps or key mutation in this branch. Publish only a complete replacement prefix, then destroy the old root. Leaf size comes from its value count; no internal-root size adjustment follows.

There is no dependency on new ownership moves, drain cursors or the transaction draft. Successful public operations outside compact-root growth, including full-node splitting and nonfull middle insertion, retain their existing behavior and open findings.

## Qualification

1. Sweep compact capacities, insertion positions, descending order and duplicates. Check exact object identities, alignment, counts, source contents and allocator conservation.
2. Refuse replacement allocation, incoming construction and every original-value copy. Verify zero lost allocations, retry, canonical-empty move-only-set recovery, contained lvalue aliases and explicit moved arguments.
3. Close the actual `SnapshotPatch` refusal with short and legal-length string keys. Re-run publication failure injection and report remaining independently attributed defects as failures.
4. Run three-platform native/sanitizer and mixed-mode profiles. Measure root growth and actual nested-definition allocation/CPU/peak costs against the preceding immutable revision.

This component does not close full-node split, nonfull middle insertion, rebalance, erase or their const-key lifetime findings. The [release risk evaluation](BTREE_RELEASE_RISK.md) remains the acceptance boundary.

## Qualification results

The [frozen evidence](../benchmarks/btree-isolated-growth-20261022.json) records 24 layout profiles, 96 mixed-mode runs, actual-type allocation refusals and the unsuppressed Linux publication leak gate. Three allocation-ledger repetitions preserve allocation counts and retained bytes, with higher temporary peaks from overlapping old/replacement ownership. Synthetic CPU pairs remain exploratory; the actual-type CPU phase is rejected on interference. Remaining native mutation paths keep their separate release evaluation.
