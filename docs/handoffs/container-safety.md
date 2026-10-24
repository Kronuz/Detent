# Ask: qualify an ordered-container implementation or select a replacement

Review a C++20 application using a B-tree for public model fields. The relevant shapes are string-keyed record tables, string-keyed patch tables, nested string-keyed scalar override tables, and caller-supplied override tables. Records contain scalar variants and nested tables. Some consumers publish immutable snapshots; others incrementally construct and retire definitions under work budgets.

This brief names no application or repository. Establish the actual container revision, value types, consumers and failure paths from the supplied source. Do not infer tested headers from a dependency pin when build-system source overrides exist.

## Problem and decision boundary

The safety questions are full-node insertion/erase involving const keys, object lifetime during relocation, and cleanup after allocation or copy failure. Determine which are reproducible for the application's actual types. Generic guarantees for arbitrary throwing user types are a separate library concern.

A transactional mutation strategy can solve lifetime problems while adding unacceptable allocation, copying and memory costs. Its adoption requires measurements. Standard ordered maps provide another safety option. A hash index may improve lookup or random construction but introduces collision policy, ordering and publication costs. No representation is assumed to win.

## Requested work

1. Reproduce each actual-type correctness gap independently, including denied-allocation paths. Separate language-level lifetime defects from stronger exception guarantees the application does not require. Retain useful isolated repairs.
2. Recover existing experiments before writing more prototypes. Compare the corrected B-tree, a standard ordered map, and a hash candidate only where applicable. Keep one production representation after selection.
3. Measure the complete lifecycle: parse, validate, construct, copy, patch, publish, transfer, prune and destroy. Include nested overrides, short and maximum supported names, random/ordered insertion, broad and hot-key reads, and misses.
4. Recommend a representation from explicit CPU, memory and safety evidence. Further structural B-tree repair needs a demonstrated constraint that rules out the simpler safe alternatives. Hash adoption requires an adversarial-key policy and sorted publication costs.

## Acceptance

Use at least a 100K-record comparison to expose index costs, then identify what larger-system qualification remains necessary. Measure allocation requests, retained/peak heap and CPU separately from RSS. Include coexisting defaults, live snapshots and histories. Charge hash publication's sorting scratch and CPU to that candidate.

Require equivalent duplicate rejection, ordering and evaluation, plus byte-identical valid serialization, command digests and mapped publication where those are compatibility requirements. Exercise allocation failure during parse/copy/patch/publication, partial discard, shared captures and retirement. Run unsuppressed leak checks on a supported platform. Leak-disabled ASan is useful memory-safety coverage, but cannot qualify leaks.

Incremental definition cleanup must retain its accounting charge until disposal completes. Positional map erase being amortized constant does not prove a strict time bound; measure maximum cleanup turns at supported override limits. Whole-snapshot destruction remains linear unless a separate mechanism budgets it.

Changing public container types changes C++ layouts, iterators and concrete-type interoperability. State that compatibility break and rebuild every affected consumer. Preserve durable bytes and wire behavior. Remove unused direct dependencies after selection; retiring the upstream container project is outside this task.

Work serially in an isolated checkout, follow repository/shared-library instructions and coordinate host load. Return a concrete decision, isolated fix commits, actual-type failure tests, reproducible before/after results and compatibility notes. If no candidate meets a budget, name the budget and measured failing case.
