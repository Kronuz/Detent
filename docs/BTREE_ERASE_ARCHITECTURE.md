# Btree erase and bounded reclamation: validated architectural blueprint

## Abstract

Extend the shared mutation transaction to erase, merge and root collapse. A separate ownership-taking drain cursor provides allocation-free incremental reclamation without relocating actual const-key pairs. Detent's `noexcept` retirement paths must migrate with this change; ordinary transactional erase can allocate. Keep public semantic commands and definitions unchanged.

| Review | Status | Date |
| --- | --- | --- |
| Scalability, edge cases, dependencies/state and security/scale | Validated for bounded implementation; runtime and cost gates open | 2026-10-21 |

## Shared mutation mechanism

Generalize the [insertion images and materializer](BTREE_EXCEPTION_SAFETY_ARCHITECTURE.md) to optional incoming storage, one erased source identity and an explicit final size. Extract removal, merge, underflow repair and root collapse into the same node kernels, preserving balancing choices. Internal deletion swaps logical identities with the predecessor, then removes the victim identity. Remove obsolete native const-key mutation bodies once public mutations use the transaction.

The fast policy gathers survivors, destroys displaced originals and the erased original exactly once, reconstructs actual const-key pairs, then publishes metadata and retires allocations. Generic replacements retain originals until commit; old-node retirement destroys erased values without a second victim destruction. Preserve the established copyable-value, throwing move-only mapped and destructive set-recovery guarantees.

Save the successor's source identity or an end marker before planning and resolve its final position before consuming originals. Do not search by key after commit, especially with duplicates. Leaf collapse installs the survivor's self-parent sentinel; internal collapse can reuse the root allocation; final deletion installs null root/rightmost. Range erase counts successive erasures using returned iterators and checks narrowing into the existing `int` API before mutation.

## Allocation-free ownership drain

Expose a default-empty owning `drain_type`, acquired from the container in O(1) without traversing `bytes_used()`. Prepare allocator state before detaching ownership and emptying the source. Keep the original root identity, current node and traversal phase; use parent links to destroy live value prefixes and release exact node extents incrementally. Never consult the root-parent sentinel after its leaf is freed.

`step(limit)` returns actual work consumed. Count each descent, pair destruction and node free separately; zero budget performs no work. The cursor destructor completes its owned traversal. Ordinary clear and the cursor use the same iterative postorder walker, with explicit root-allocation metadata for subtree cleanup. Move and abandonment must retain exact allocator ownership. Custom allocator-copy behavior remains part of acquisition's conditional exception contract; Detent's allocator must pass allocation-disabled acquisition and stepping tests.

## Detent ownership and accounting

Use one concrete `detail::DefinitionArena final : DeferredValues<KnobDef>` with an inline override drain and bounded collection method. State copies and ExportCapture share that arena and cursor under the existing single-executor contract. Hide direct head-advancing collection on the concrete arena. Keep inherited creation, retained-byte accounting and final head deletion canonical; detaching overrides does not refund the definition's charge early.

Keep one drain after pending/command storage in CommandDecoder::Impl, one after validated command storage in CommandApplication::Impl, and one beside the service's retired-task head. Pass the persistent service cursor into `dispose_command`; do not put move-only state into Command or BoolDef. Service an active cursor before inspecting its emptied source map, and never advance the owner until the cursor completes. Declaration order must drain owned nodes before containing semantic objects unwind.

Charge one scheduling unit and 128 budget bytes for a nonempty handoff, then one unit and 512 budget bytes per cursor action, using `min(units, bytes / 512)` and subtracting actual work. Decoder discard uses records; other collectors use receipts. Keep existing final-definition and final-mutation charges. Scheduling credits remain distinct from retained-memory accounting. Measure cursor and owner padding, and include fixed storage in each owner's existing capacity account.

## Scale and qualification

Single mutations retain O(node capacity × height) affected state; header-only nodes do not expand child arrays. Drain acquisition is O(1), uses no heap work stack, and performs counted actions. Arbitrary mapped destructors do not gain a constant-time guarantee; Detent's bounded override values require their own checks.

1. Attribute positioned failures through internal deletion, both rebalance directions, cascading merges and both collapse forms; verify successor identity, duplicate order, live objects and exact allocator conservation.
2. Disable allocation throughout drain acquisition and stepping. Exercise zero/tiny budgets, cursor moves, abandonment, stateful allocators and over-alignment.
3. Alternate collection through State copies and ExportCapture sharing one arena; prove the head and accounting cannot advance early. Check decoder pending-to-command and service-head transitions and partial-cursor unwind.
4. Freeze final combined headers for three-platform native/sanitizer/mixed-mode gates, Detent publication LeakSanitizer, retirement tests and matched CPU/allocation/peak costs. No suppression or parallel erase fallback.

Xapiand at `c625977e7d1254796dabb65352b0b26aabc0954b` has no cpp-btree include or target in source/CMake or inspected built dependency metadata; its Xapian B-tree is unrelated. Its consumer gate is N/A for this library. Strata at `88a56e7` uses cpp-btree only in the opt-in `kv_proto_buffer` research target, which must repin when the fix is published.

## Implementation review and preliminary qualification

The component receives source GO with no confirmed P1/P2 production defect. [Preliminary evidence](../benchmarks/btree-erase-reclamation-preliminary-20261021.json) records four native and four sanitized macOS library profiles, four selected Detent consumer tests, and an expanded allocation-denial fixture for actual shared-arena collectors and partial command disposal. Production headers are identical across those source snapshots; newer fixtures extend coverage without changing the implementation. Final platform, mixed-mode, failure, publication LeakSanitizer, footprint and matched-cost gates remain open. No immutable repaired pin is adopted yet.

## Adoption decision after final qualification

The [final functional gates](../benchmarks/btree-erase-reclamation-qualification-20261021.json) pass across all three platforms and the composed Linux consumer, including unsuppressed LeakSanitizer. The [matched cost report](../benchmarks/btree-transaction-cost-20261021.json) rejects production adoption: normal mutation CPU is about 5.75 times baseline, with per-operation allocation churn. Persistent copy bytes match baseline; the default cursor adds 24 bytes to each measured owning type. Timing remains exploratory under recorded desktop activity.

Land range hints, comparator checking, first-entry unwind, iterator/count corrections, ownership moves, compact copy and owning drain as separately qualified components on the original library. The staged insertion/erase rewrite remains unpublished. Its later optimization retains one transaction engine and must repeat failure, allocation, footprint and consumer gates.
