# Model containers: validated decision blueprint

**Date:** 2026-10-22. **Status:** Comparison in progress. Further btree structural implementation is on hold.

## Decision scope

Choose containers for Detent's model tables before replacing more of btree's mutation engine. Independently reviewed btree fixes remain useful upstream. Detent's release does not require selecting that library if a standard container meets the measured operating budgets.

| Model field | Uses |
| --- | --- |
| `Snapshot::knobs` | Standalone client snapshots, classic distributor publication and legacy shared-memory serialization |
| `SnapshotPatch::knobs` | PATCH parsing, application, composition and history |
| `BoolDef::overrides` | Group evaluation, authority definitions, encoding and bounded disposal |
| `OverrideTable::values` | Per-process CLI overrides |

The normal agent-connected live read uses a mapped image. Classic readers search sorted records; managed readers use immutable pages. Model maps still hold optional defaults and CLI overrides. The authority's main records, namespaces and producers use its separate immutable `PersistentMap`, an AVL tree with shared roots and deferred collection. Its library extraction remains independent.

## Candidates and state boundaries

1. **Btree `25f9494`.** Keep the reviewed ownership fix as the strongest existing baseline. Its seven hosted jobs, three-platform local matrix, composed Linux leak checks and clean all-product build with 208/208 tests pass (627.94 seconds). Remaining full-node and erase findings stay open.
2. **Standard ordered maps.** Replace the four fields with `std::map`, preserving `std::less<>`. Inspected callers use ordinary ordered-map operations. Validate exact ordering and bytes rather than presuming compatibility from compilation.
3. **Hash lookup index.** The cost prototype changes only `Snapshot::knobs` to `std::unordered_map`, with a transparent adapter over standard string-view hashing and default load-factor/growth policies. The other fields remain standard ordered maps. JSON serialization, shared-memory writing and authority imports use a shared sorted pointer traversal in the completed publication prototype. Sort before emitting both shared-memory records and their blob so offsets and padding remain identical. The current lookup-based diff already handles arbitrary snapshot iteration order. Assess collision-heavy inputs and keyed hashing before selecting a network-input policy.

Sorted contiguous storage remains a candidate for immutable tables. Evaluate it by workload rather than imposing one representation everywhere. A replacement must preserve incremental construction and disposal budgets; bulk sorting or vector relocation cannot silently become an unbounded executor turn.

Changing these public field types changes enclosing C++ layouts, iterator types and concrete-type interoperability. Rebuild every C++ consumer and shared-library boundary together. Preserve valid wire, checkpoint, command-digest and shared-memory bytes. Aliases do not conceal this compatibility break. Keep one production representation after selection.

## Bounded disposal and scalability

The four authority disposal sites drain only `BoolDef::overrides`: decoder discard, the shared definition arena, retired command disposal and application cleanup. Each entry owns a bounded string and scalar variant. Standard positional erase, or extraction followed by scoped node destruction, needs no new per-State, per-arena or per-command cursor. Preserve the existing accounting charge until the definition is fully disposed.

Positional removal is amortized constant, not a strict wall-clock guarantee. Measure maximum cleanup-turn latency at supported override limits with allocation and comparator use denied. Whole-snapshot final release remains linear, including nested overrides; do not claim universal bounded reclamation from node extraction.

## Evidence and exit gates

1. Compare identical frozen application sources, keys, flags and dependency closures. Use the ownership-qualified baseline rather than the historical `dd984d9` standard-map comparison.
2. Measure actual 100k-knob models with three overrides, short/maximum-length names, ordered/random construction, final snapshot transfer and pruning. Record complete C++ heap requests, retained/peak bytes and CPU separately from RSS. Include simultaneously retained defaults, live snapshots, histories and reader generations when accepting budgets.
3. Measure actual `Client` and `ClientReadView` calls on a 100k-key mixed bool/int corpus. Cover hits, misses and group rollouts, with both broad access and a warm 32-key working set. The older three-knob benchmark cannot establish large-index read costs.
4. Compare persisted 100k PUT/PATCH costs, including the hash index's sorted traversal. At 100k entries its pointer array requests approximately 800 KB on a 64-bit platform. Include sorting CPU, scratch memory and failure cleanup. Preserve rejected/interfered runs separately from accepted measurements.
5. Qualify parsing, copying, PATCH and publication failures with unsuppressed leak checks. Require equivalent duplicate rejection, ordering and evaluation, plus exact protocol/storage bytes. Exercise shared arena/capture collection, partial discard and task retirement under allocation denial.
6. Accept the measured CPU and memory tradeoffs explicitly. If a standard candidate is selected, remove Detent's unused direct btree fetch/link/pin, qualify all platforms and consumers, and document the C++ compatibility change. Keep upstream btree maintained. Return to structural repair only if a demonstrated operating constraint rules out the alternatives.

The [capacity qualification](CAPACITY_QUALIFICATION.md) adds the 1K–5M primary matrix and 10M exploratory tier. A selected container must be evaluated inside that pipeline; the 100K comparison cannot establish full-system capacity.

## Accepted 100K comparison

The [frozen comparison](../benchmarks/model-container-comparison-20261022.json) records five alternating pairs with identical dependency closures. Standard-map to hash construction CPU changes from 159.997 to 113.779 ms for random short names and 350.433 to 220.443 ms for random long names. Ordered construction changes from 114.885 to 124.464 ms and 216.401 to 242.560 ms respectively. These timings include validation and destruction. The hash model retains 28,823,016 requested bytes for short names and 90,423,016 for long names, 776,984 bytes less than the standard-map model. Both retain more than btree.

Broad-access integer-hit CPU for actual `Client` and `ClientReadView` calls falls approximately 44–60% against standard maps. Broad misses cost approximately 17–23% more; hot32 results differ. The report retains all six workloads and all four name/access combinations rather than selecting only the winning case. Read checksums match across both arms and all pairs.

The hash index therefore merits complete-path qualification, but standard hashing is not an accepted collision policy. Sorted publication correctness passes after the independent [reserved-byte correction](SHARED_MEMORY_PADDING.md). Initial persisted-publication timing batches are rejected for WindowServer interference. No production representation is selected from these incomplete costs.
