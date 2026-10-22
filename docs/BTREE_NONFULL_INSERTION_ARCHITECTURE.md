# Guarded nonfull-leaf insertion: validated architectural blueprint

**Date:** 2026-10-22. **Status:** Validated for bounded implementation; qualification and cost gates open.

## Abstract

Extend the guarded replacement leaf to insertion into a nonfull leaf when values must shift. Keep tail append direct, and advance internal-root size only after construction succeeds. This removes const-key swapping from this branch without a planner or a whole-tree transaction. The [compact-root repair](BTREE_COMPACT_ROOT_ARCHITECTURE.md) supplies the construction and failure-ownership mechanism.

## Scalability

Allocate one replacement leaf at the existing capacity and construct its final values in O(node capacity). Reuse one guarded construction helper for both geometric compact-root growth and same-capacity insertion. Incoming-first construction preserves contained lvalue aliases; a separate live-slot flag and prefix count own every completed value.

This can add node/key/nested-value allocations to ordinary middle insertion. Measure those costs against the published independently repaired btree before extending replacement to structural mutations. Tail append allocates no replacement leaf. The rejected transaction engine remains excluded.

## Edge cases and publication

Keep the original leaf linked until replacement construction completes. A leaf root installs the replacement root. A nonroot leaf updates its parent's existing child slot and position, then updates the root's leftmost/rightmost references where needed, before destroying the original. Increment internal-root size only after successful publication. Return an iterator into the installed leaf.

Copy failures preserve original contents, links and size. Throwing move-only mapped values can remain moved-from while keys and topology stay valid. If a potentially throwing noncopyable set-key move consumes an original key and fails, clean the replacement and canonically clear the original tree. Explicit moved arguments can be consumed; moving contained set keys remains unsupported.

A nonfull tail append constructs directly and advances overall size after success. Full-node split and promotion paths keep their existing accounting and remain outside this repair. Do not route a split's partial structural mutation through this component and claim rollback.

## Dependencies and state

Preserve public interfaces, node layout, allocator ownership and iterator invalidation rules. Construct actual const-key pairs. Supply the original leaf, insertion position and replacement capacity explicitly to the single helper. No ownership-move API, drain cursor, image registry or staging arena is a prerequisite.

## Security and qualification

1. Exercise leaf roots and nonroot first/interior/last children, both comparator orders, duplicates and contained aliases. Refuse replacement allocation, incoming construction and every original-value construction. Check unchanged published links/size after copy failure, allocator conservation, basic move recovery and reuse.
2. Verify direct append refusal leaves internal-root size unchanged and successful append needs no replacement allocation. Cover nonroot boundary pointers before any old leaf is freed.
3. Sweep actual Detent long-key/nested-definition allocations across enough entries to create internal roots, then run the composed publication/retry/diff-cache suite with unsuppressed LeakSanitizer.
4. Run native/sanitizer and mixed-mode profiles across macOS, Linux and FreeBSD. Measure matched mutation and actual 100k-knob/three-override workloads, reporting calls, requested/retained/peak bytes and accepted CPU samples.

Full-node split, parent promotion, rebalance and erase remain release findings. If one-leaf replacement has unacceptable churn, complete the [standard-map alternative](BTREE_RELEASE_RISK.md) rather than broadening a costly repair. Public type compatibility and its measured 8.58-9.00 MB additional retained allocation footprint remain explicit tradeoffs.
