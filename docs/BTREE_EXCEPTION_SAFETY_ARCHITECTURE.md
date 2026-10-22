# Btree exception safety: validated architectural blueprint

## Abstract

Detent's publication failure sweep exposes pre-existing lifetime defects in the pinned cpp-btree. The repair combines compact ordered copy construction, real container moves and affected-node insertion staging over stable entry identities. Component source review accepts these mechanisms; production pin adoption waits for the combined platform, cost and consumer gates, including the full publication sweep with Linux LeakSanitizer enabled.

| Review | Status | Date |
| --- | --- | --- |
| Scalability, edge cases, dependencies/state and security/scale | Validated for bounded implementation; complete runtime qualification required | 2026-10-21 |

The published baseline is `dd984d92540674d0794ce338712691ae9afa3d10`. These changes remain isolated upstream drafts. [Dependency upgrades](DEPENDENCY_UPGRADES.md#publication-failure-matrix-btree-lifetime-leaks) records the original failure evidence.

## Ownership repair

Copies build a compact equal-depth shape from entry count, without comparisons or reinsertion. For capacity `B` and nonzero size `n`, use `L = n / (B + 1) + 1` leaves, distribute `n - (L - 1)` values evenly across them, and build parent levels with `1 + (children - 1) / (B + 1)` nodes until one root remains. Distribute child counts evenly at each level. This uses O(height) allocator-backed planning storage; singleton roots retain geometric capacity.

Consume one const source iterator in child/separator order, constructing each actual value directly in its final slot. Track constructed prefixes and completed children independently. Cleanup frees only owned values/subtrees with matching allocator extents; finalize root parent/rightmost/size metadata before publishing the root. Assignment retains clear-then-copy semantics and leaves an empty destination after failure. Measure tiny/nested planning allocations as well as populated and erase-thinned retained footprints.

First insertion bypasses empty-tree lookup before entering the existing value-construction boundary, so a comparator-copy failure cannot strand a zero-count root. Failed first value construction restores canonical null-root emptiness. Range insertion reacquires its end hint after every mutation. Compact growth, separator transfer and internal-root promotion use the insertion transaction below.

## Container moves and compatibility

Add moves through `btree` and every copy-declaring container wrapper. Keep explicit copy operations. Move preparation copies comparator and allocator state before stealing nodes; assignment first clears the destination with its old allocator. Self-move does nothing, and emptied sources remain reusable. Node-local parent/rightmost pointers need no rebasing.

Retain the library's existing allocator ownership policy: allocator state travels with the tree, including unequal stateful allocators. Copy assignment and member swap remain unchanged. This is a legacy propagation contract, not full standard allocator-trait conformance. Implementing nonpropagating unequal-allocator relocation would be a separate compatibility decision. Derive `noexcept` from actual comparator/allocator copy operations, including adapters, rather than assuming user-defined moves cannot throw.

Prove nothrow move construction, assignment and swap for Detent's overrides map, `BoolDef`, `KnobDef`, `optional<KnobDef>`, `Snapshot` and `SnapshotPatch`. `pair<const string, KnobDef>` remains potentially throwing: its const key is copied during relocation. Moving populated nested maps must preserve element addresses without allocating nodes or transferring elements.

## Affected-node staging

Use stable entry IDs while planning. Sparse node images retain an original allocation identity, planned count, independent slot IDs, child references and parent/position metadata. Changing a planned count does not destroy IDs that a later transfer still needs. Images cover the insertion path and selected siblings, O(node capacity × height), with untouched subtrees borrowed.

Refactor the existing insertion, split, rebalance and child-link kernels over node/slot primitives. Image moves and swaps permute IDs; they do not materialize values. Keep one algorithm body per operation, with native adapters for unchanged erase callers. After the final destinations are known, every old entry and the incoming entry must occur exactly once.

```text
Plan stable slot identities over affected node images
                         |
Prepare destination storage and required key copies
                         |
Materialize final pairs; publish links/counts and size
```

An unchanged entry at its original allocation/slot needs no reconstruction. For changed entries, prepare mutable keys and mapped scratch before consuming original values. Stable per-node or allocator-rebound storage keeps entry identities valid; a growing vector must not invalidate them. Duplicate `try_emplace` still avoids constructing the incoming value.

For Detent's proven nothrow key/mapped moves, gather relocated mapped values into prepared scratch, destroy their original pairs, and construct actual `pair<const Key,T>` objects once in final slots. Install active child edges, parent positions and root metadata, publish insertion size, then retire replaced allocations by exact kind/extent. The engine performs no allocation or comparison after gathering starts. Mutable scratch must never be reinterpreted as a public const-key pair.

For potentially throwing mapped transfers, materialize fresh native nodes for affected images while original topology stays live. Copyable values retain their original contents on failure. Move-only throwing mapped values can leave mapped state moved-from while old keys, counts and lifetimes stay valid, a basic guarantee. Each replacement guard destroys only its own constructed prefixes and allocation; it never recursively clears borrowed children. Commit and retirement update pointers/headers and free exact replaced nodes without freeing surviving subtrees.

| Insertion shape | Treatment |
| --- | --- |
| Append into a nonfull leaf | Direct construction, then count/size publication; no planner |
| Middle insertion | Permute IDs and reconstruct displaced entries under the selected policy |
| Compact-root growth | Plan geometric-capacity replacement and materialize once |
| Full leaf-root split | Prepare root, sibling and incoming value before publishing either node |
| Internal-root promotion | Plan demotion and following split together, replacing the destructive native swap |
| Either rebalance direction | Retain delimiter IDs; defer borrowed-child parent/position updates until commit |

Resolve the returned iterator from the incoming entry's final destination. Preparing every final key once handles entries relocated repeatedly by the logical algorithms. Measure transient images/key copies/scratch separately from retained node bytes; generic replacement can also duplicate mapped objects' owned storage.

A move-only set key whose throwing move changes ordering needs a canonical-empty fallback after staged cleanup to preserve a usable basic guarantee. Map relocation still needs a valid way to construct actual const keys. Unchanged erase/merge const-key mutation remains open; the insertion design does not close that wider finding. Add no blanket type restriction to hide these paths.

Lvalue aliases to contained mapped values must survive relocation through preparation of the incoming value. Explicitly moving a contained argument can consume it before a later failure. Moving a contained set key through the legacy mutable iterator violates ordering before staging can protect it. Prove that duplicate `try_emplace`, including hinted calls, performs no mapped construction or movement.

## Qualification and adoption

1. Attribute every regression on the untouched baseline and candidate: full-leaf separator transfer, root-then-sibling allocation, middle/beginning suffix transfer, parent separator insertion, tail construction, internal-root promotion and both rebalance directions.
2. Check exact allocation extents/alignment, live-object identities, contents, iteration distance, metadata and retry behavior. Include nested maps, duplicate-key containers, successful copies followed by mutation/collapse, unequal allocators and throwing comparator preparation.
3. Run C++17/C++20 Debug/Release and mixed-mode native/sanitizer profiles on macOS, Linux and FreeBSD. Preserve valid explicit construction with non-default allocators, including nested-map trait evaluation under Clang/libstdc++.
4. Re-run Detent's complete publication failure sweep with Linux LeakSanitizer active. Rebuild Xapiand and clean immutable-pin consumers. Do not suppress the original failure or equate a narrow fixture pass with application closure.
5. Compare identical mutation, populated/pruned-copy and nested-definition move workloads against the old headers. Record CPU, allocations and requested/peak bytes separately; copying comparisons include comparison/destruction, while move preparation excludes source construction.
6. Review the complete component before publication. Land separate fixes, update the shared-library catalog and then adopt immutable pins. First-release qualification remains separate.
