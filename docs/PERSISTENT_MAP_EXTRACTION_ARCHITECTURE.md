# Validated Architectural Blueprint

**2026-10-28 · APPROVE WITH CONDITIONS, planning only.** Extract one C++20 header-only `persistent-map` implementation; preserve Detent’s immutable AVL tree and owner-driven reclamation. Reviewed Detent:f1487c2. No builds, tests or measurements run; no current consumer defect established. Publication and adoption remain gated.

## API and extraction boundary

```cpp
template<class Key, class Value, class Compare = std::less<>,
         class Alloc = std::allocator<std::byte>>
class PersistentMap; // target persistent_map::persistent_map
// Constructor: PersistentMap(Compare = {}, Alloc = {});
```

Preserve `set/erase -> PersistentMap`, heterogeneous `find/cursor_from`, `cursor`, `for_each`, `size/height`, `retire_root`, `collect(budget)`, and existing diagnostics. Detent’s header becomes an alias; only one tree engine exists. Store comparator and allocator once in the shared arena; all descendant versions and cursors retain them. Require a stable strict weak ordering across key and lookup types. Stateful comparators work through the stored instance. Key/value copies may throw; destruction must not throw.

Copies share the originating arena and allocation policy; assignment replaces that ownership pair. This is a persistent-version API, without mutable-container allocator propagation. Moved-from handles support destruction and assignment only. Borrowed rows remain valid while their retaining root/cursor survives.

## Scalability Check

The AVL mechanism gives logarithmic lookup, update paths and lower-bound positioning; updates copy paths, while retained versions preserve shared subtrees. Preserve the 128-entry cursor stack and its checked overflow (`persistent_map.hh:139`). Measure aggregate retained nodes across versions, not merely current `size()`.

`collect(n)` destroys at most n tree nodes while its arena remains alive. This does not bound payload destructor time or bytes. Preserve node-based diagnostics: `allocations()` counts constructed nodes; `retained_nodes()` includes queued nodes. Detent charges one receipt and 128 budget bytes per collected node (`state.cc:814`, `export_capture.cc:57`); these are application work units, not physical allocation accounting.

## Edge-Case Analysis

All ownership changes, traversal and collection remain confined to one serialized executor. Shared reference counts do not establish concurrent arena safety.

The last map/cursor releases its root before its arena. When the last arena dies, weak-owner locking fails and descendant deleters free directly (`persistent_map.hh:38,64`). Final teardown can therefore reclaim every remaining node synchronously; do not advertise bounded last-handle release. Keep a collector-owning version alive, drop all cursors and other versions, retire its root, then drain before final release. Cover cross-arena assignment and move destruction too.

Use rebound standard allocators for arena storage/control, nodes and node controls. Node deleters retain the originating allocator as well as the weak arena, so expired-arena destruction deallocates correctly. Require nonthrowing allocator copies/deallocation and ordinary pointer allocators initially. Allocator resources must survive every allocation, including weak control blocks. Nested payload allocation remains the payload’s responsibility.

Guard raw allocation through construction; transfer ownership exactly once into `shared_ptr`. Inject failure at control allocation: its deleter must enqueue or destroy the constructed node once. Exceptions preserve previous logical roots; deferred garbage may affect counters until collection.

## Dependency & State Audit

The supplied fresh catalog/repository check finds no persistent-map library. `allocators/retirement.h:48,130,162` instead provides intrusive worker-owned retirement with runtime/domain lifetime and no caller `collect(n)`. Substituting those handles changes executor and budgeting semantics. Keep the existing queue private to tree destruction; add no retirement framework, executor or ledger. Canonical accounting adapters compose through `Alloc`, without a mandatory dependency. EX-1 adoption remains independently gated.

## Security & Scale Review

1. Oracle-test randomized operations against `std::map`, retaining historical roots/cursors; verify values, order, subtree counts and balance. Cover throwing comparators/copies, every allocation failure, allocator identity, zero budgets, last-handle teardown and allocation-denied traversal/collection under sanitizers.
2. Preserve exact checkpoint/export bytes and authority failure/lifetime behavior. Require supported-platform library and consumer CI, immutable dependency pins, provenance and catalog updates.
3. Run counterbalanced A/A and A/B under exclusive host holds: current production configuration versus extraction alone. Sweep actual key sizes, batch sizes and retained-version counts; report lookup/update/export CPU, allocation requests/bytes, peak retained heap, collection turns and final teardown separately. Reject unexplained cost increases; retain existing consumer thresholds.

**Limit requiring owner decision:** guaranteed bounded final release would change this contract. Stop that extension for an explicit reclamation decision; it is unnecessary for the preservation extraction.
