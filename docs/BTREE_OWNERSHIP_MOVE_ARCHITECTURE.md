# Ownership moves: validated architectural blueprint

**Date:** 2026-10-22. **Status:** Bounded implementation and qualification in progress against `cbc3576`.

## Decision and scalability

Add genuine ownership moves through the complete container wrapper chain. Core construction copies comparator and internal allocator state into an initially null destination handle, then transfers the root and nulls the source. Node links refer to nodes, so transfer needs no traversal, reparenting or value construction. Assignment first destroys the destination with its previous allocator; populated-destination assignment therefore includes destruction work.

This component adds no drain, mutation planner or representation change. It does not repair full-node insertion or erase. Public layout remains unchanged, and all consumers rebuild against one header revision.

## Edge cases

Preparation failure leaves source ownership untouched. Assignment preparation failure leaves the destination empty while the source still owns its original nodes. Copy state before transfer rather than moving comparator/allocator state out of a source that might need recovery. Preserve self-move, empty-source state adoption, distinct stateful allocators, source reuse and const-rvalue copy behavior.

Conditional `noexcept` follows comparator/internal-allocator copy construction or assignment under the existing nonthrowing destruction/deallocation requirement. It does not depend on mapped-value move traits. Assignment retains the existing state-propagation convention, including source allocator adoption for unequal allocators; this does not introduce standard-container nonpropagating element relocation.

## Dependency and state audit

Default moves forward through `btree_container`, unique/multi wrappers, map wrapper and all four public families. Explicitly preserve copy assignment when adding moves. Keep copy and member-swap behavior intact.

Comparator-adapter constructor bodies and default-initialization behavior remain unchanged. Derive their exception declarations from the actual initializer expression: copying an adapter initializes `Compare(c)` with a derived argument, which can select a templated comparator constructor rather than the raw comparator copy constructor. Do not default that constructor or substitute a raw-comparator trait.

Rvalue construction/assignment now empties its source intentionally. Const rvalues retain copy behavior. Pointer transfer preserves node capacity and element addresses; measure retained/peak bytes rather than presuming that replacing a compact copy preserves its footprint.

## Security and qualification

1. Cover all four families, empty/compact/internal roots, duplicates, populated destinations, self-move, source reuse and over-alignment.
2. Deny node allocation and element copying during transfer; require exact node/element addresses, live identities and allocator conservation.
3. Refuse comparator and allocator construction/assignment independently, including derived-adapter constructor selection. Check source ownership after every refusal and destination cleanup with its previous allocator.
4. Exercise stateful and nondefault allocators without bundling the separately attributed legacy constructor-trait limitation or unused-overload defects.
5. Prove actual Detent move/assignment/swap traits for overrides, definitions, optional definitions, snapshots and patches. Keep the negative assertion that moving a const-string-key pair can still throw.
6. Run native/sanitizer/mixed profiles on macOS, Linux and FreeBSD, composed Detent tests, immutable CI and the candidate's complete downstream rebuild.
7. Compare construction and assignment separately, accounting for previous-destination destruction. Repeat actual 100k-knob/three-override and publication costs against the preceding repaired source. Near-resolution pointer timings do not support precise speedup ratios.

Completion requires ownership transfer, truthful default-type traits, preserved failure/state contracts, unchanged layout and independently qualified cost. Remaining const-key structural mutations and bounded reclamation stay release gates.

## Early cost gate

The [frozen R1 results](../benchmarks/btree-ownership-moves-20261022.json) compare against the adopted `cbc3576` header in five alternating pairs on the Intel Mac. Constructing, validating and destroying 100k actual definitions with three overrides takes 33.9–61.3% less process CPU across ordered/random and short/maximum-length keys. Retained requested bytes match in all four workloads; the random maximum-length case reduces allocation calls from 4,590,088 to 1,399,396. These are local model costs, with no publication-throughput, fleet-cost or power claim.

Longer mutation and copy medians remain within about 1% of baseline. The existing nested round-trip move benchmark reaches clock resolution and does not establish a precise speedup ratio. Separate construction, empty assignment and populated assignment measurements, platform qualification and the immutable-pin full product rebuild remain required before adoption.
