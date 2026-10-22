# Btree release risk evaluation

**Date:** 2026-10-21. **Status:** Application safety boundary reviewed; standard-map prototype selected for comparison, not adoption.

## Decision

Keep the separately reproduced ownership and first-insertion repairs. Reject the staged mutation implementation at its [measured cost](../benchmarks/btree-transaction-cost-20261021.json). A production release needs safe behavior for Detent's actual types and failure paths; it does not require every possible stronger exception guarantee for arbitrary library clients.

| Finding or feature | Detent relevance | Disposition |
| --- | --- | --- |
| Failed copy leaks | Publication fault injection reproduces 222,608 bytes in 1,032 allocations on the original library | Required repair or container replacement; the combined draft's leak closure does not qualify an extracted implementation |
| Failed first-value construction publishes an empty root | Actual string/value or node allocation can throw; cleanup subsequently assumes a valid container | Required narrow repair; construct before publishing the root |
| Const-key mutation | Map values are actual `pair<const Key,T>`; native swapping casts away the key's constness | Required defined behavior or replacement on Detent's ordinary mutation paths; a warning does not remove this issue |
| Split/growth/rebalance failure cleanup | Snapshot and command construction allocate and can fail; rejected work must remain safely destructible | Required basic lifetime/topology safety for actual Detent types; require positioned failure evidence |
| Real ownership moves | Current nested containers can be copied when callers request a move | Optimization and prerequisite for any proposed nonthrowing-transfer policy; not independently proof of a current data-loss bug |
| Owning drain | Existing collectors erase entries under a work budget; a large map must not turn one cleanup step into a full traversal | Required bounded-reclamation behavior at the planned scale; cursor design is one implementation choice |
| Broken iterator-returning overloads | Those specific overloads have no identified Detent use | Independent library cleanup; do not block release solely on unused APIs |
| Arbitrary throwing move-only keys or mapped types | Detent uses string keys and closed definition/override types | Stronger generic guarantees are optional scope; document supported guarantees and compatibility if changed |
| Claimed count subtraction defect | The original source already calls `distance` | Withdrawn; no repair without a separate reproducer |

## Warning for the current production pin

Detent still pins `dd984d9`. The published range and comparator fixes do not repair copy leaks, const-key mutation or general mutation failure cleanup. The original fault-injection leak remains relevant to that production pin. Successful native or sanitizer runs do not establish that modifying an actually const key is defined behavior. Treat this as an open production-qualification issue, not a claim of a demonstrated external exploit or observed successful-operation data corruption.

A failed private snapshot can be discarded only if all live objects, node metadata and allocation ownership remain valid enough to destroy it. Publication isolation protects the previous snapshot; it does not by itself establish that failed temporary containers are safe. Rejecting invalid JSON or bounding input size likewise does not prove that later allocation cannot fail.

## Alternatives being evaluated

1. **Localized prepared reconstruction.** Preserve native balancing and prepare only the affected path's nodes, keys and value storage before destructive work. Reconstruct actual const-key pairs with proven nonthrowing transfers for Detent's types. Use bounded contiguous storage instead of per-entry linked allocations. This requires a fresh design and positioned-failure evidence; it has no measured performance result yet.
2. **Standard ordered containers in Detent.** Replace application btree uses with `std::map` and use standard ownership/extraction mechanisms for bounded cleanup. Measure actual nested definitions, retained bytes, publication CPU and reclamation budgets before selecting it. No footprint or throughput win is presumed, and application source/API dependencies must be inventoried.
3. **Retain the staged design with cheaper temporary storage.** This preserves its completed functional evidence but needs allocation and CPU requalification. Standard PMR or packed arrays are hypotheses, not an accepted resolution of the cost regression.
4. **Defer broader generic guarantees.** This is acceptable only after Detent's ordinary operations and failure cleanup are safe, with explicit documented guarantees for remaining user-defined throwing types. Keeping the current unsafe application paths and adding a warning is insufficient for production qualification. A development-only build may retain the existing pin with this warning and an explicit unmet release gate.

## Current next component

Prototype replacing the four public map-bearing fields with `std::map` in an isolated source tree. Compare real 100k-knob PUT/PATCH publication and core behavior before choosing an implementation. This is an explicit public C++ field-type compatibility change if adopted; serialized bytes, key ordering and CLI behavior must remain compatible. Production types and pins remain unchanged during the prototype.

After the isolated ownership-move/copy corrections, repeat remaining mutation-failure attribution with actual Detent values and long legal keys. The generic throwing-mapped separator reproducer alone does not prove that precise remaining failure for Detent. Preparing one swap at a time is insufficient if a later operation can throw after earlier topology changes; the localized alternative must prepare the whole affected operation before consuming values.

## Acceptance evidence

Freeze each extracted candidate, reproduce its targeted defect on the preceding revision, and qualify it independently. Compare native/sanitized C++17/C++20 profiles, mixed modes, exact allocator conservation and real Detent consumers. Mutation alternatives must pass actual string/nested-definition failure sweeps and matched CPU/allocation/peak comparisons before pin adoption. No stronger generic guarantee is required merely because the rejected draft implemented it.
