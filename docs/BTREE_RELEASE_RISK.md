# Btree release risk evaluation

**Date:** 2026-10-21. **Status:** Application safety boundary reviewed; standard-map prototype selected for comparison, not adoption.

## Decision

Keep the separately reproduced first-insertion and copy correctness repairs. Reject the staged mutation implementation at its [measured cost](../benchmarks/btree-transaction-cost-20261021.json). A production release needs safe behavior for Detent's actual types and failure paths; it does not require every possible stronger exception guarantee for arbitrary library clients.

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

Detent now pins `19db54c`, including the independent range, comparator, first-root, copy and compact-root growth repairs, after 207/207 product tests and the seven-job hosted library matrix pass. The reproduced compact-growth publication leak is closed; other const-key mutation and failure-safety findings remain open. Successful native or sanitizer runs do not establish that modifying an actually const key is defined behavior. Treat this as an open production-qualification issue, not a claim of a demonstrated external exploit or observed successful-operation data corruption.

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

## Standard-map prototype results

The [isolated prototype](../benchmarks/standard-map-prototype-20261021.json) passes 10 core functional tests. Actual-model allocation probes use 100,000 knobs with three overrides each, and verify zero tracked live bytes after destruction.

| Exactly-sized input | Original btree retained requested bytes | Standard map retained requested bytes |
| --- | --- | --- |
| Short keys | 21,022,176 | 29,600,000 |
| Legal maximum-length keys (200/128 bytes) | 84,604,336 | 93,600,000 |

This is an additional 8.58 MB or 9.00 MB per constructed snapshot in these cases, including string storage and excluding allocator rounding and instrumentation headers. Snapshot object size changes from 72 to 88 bytes; definition object size changes from 32 to 48 bytes. These are synthetic model workloads, not a physical-fleet capacity result.

Two paired 100k PUT/PATCH timing attempts stop on competing operating-system processes (`fseventsd`, then `PerfPowerServices`); neither establishes an accepted timing result. Completed samples consistently record fewer main-thread allocation requests in the standard-map variant, but the baseline lacks the independent move/copy fixes. Do not promote this into a claim that standard map outperforms a corrected btree.

The prototype remains an alternative. No public field type changes are selected or applied to production. Independently qualifying compact copy continues because it repairs the reproduced copy failure without introducing mutation staging and allows a fairer repaired-btree comparison. Bounded reclamation, actual-type allocation-failure sweeps, final memory accounting and public C++ compatibility remain required before choosing the replacement.

## Copy repair and remaining actual-type evidence, October 22

The [independent compact-copy correction](../benchmarks/btree-isolated-copy-20261022.json), cpp-btree `5aadde0`, passes its three-platform qualification without mutation staging or ownership moves. The same consumer still leaks 3,120 bytes in 20 allocations during publication fault injection. A compact-growth probe using actual `SnapshotPatch` and nested definitions leaves one allocation unfreed on both the preceding revision and the corrected copy source. This establishes a remaining application failure path independently of the generic throwing-value reproducer, while excluding the copy correction as its introduction.

Continue with localized compact-root ownership/transfer correction and actual-type failure attribution. The broad transaction draft remains rejected. Ownership moves can remove fallible nested copies and should be measured as a separate component; they do not make const-key mutation legal or prove all later key-copy failures safe. Production remains on its existing pin until coherent integration is qualified.

### Localized compact-root qualification, October 22

The [guarded growth candidate](../benchmarks/btree-isolated-growth-20261022.json) passes all 24 layout profiles and 96 mixed-mode runs across three platforms. Its actual-type refusal sweep leaves no unreleased allocation at 2, 7 and 12 failure positions for the three short/long-key shapes. The composed Linux publication, retry and diff-cache suite passes 3/3 with unsuppressed LeakSanitizer, including 131 PUT and 147 PATCH refusal positions. This closes the reproduced compact-root leak in that fixture without adopting the general transaction draft.

Actual-type construction retains the same allocation counts, requested totals and final live bytes in three repetitions. Private replacement ownership increases some temporary peaks: the two-entry long-key case rises from 2,632 to 3,600 requested live bytes. The first actual-type CPU comparison is rejected on Xprotect interference. A later guarded retry completes five pairs: short-one-entry construction measures 1.351 versus 1.420 ms per 5,000 cycles; long-one-entry construction measures 3.960 versus 4.164 ms, while the long-two-entry case measures 9.047 versus 8.874 ms. These small instrumented samples remain exploratory. Five synthetic operation pairs also have wide variation; they do not establish production throughput. Full-node splitting, middle insertion, rebalance and erase retain their separate const-key and failure-safety evaluation.
