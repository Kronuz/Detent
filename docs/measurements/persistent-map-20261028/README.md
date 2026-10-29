# Persistent-map qualification

**Date:** 2026-10-28. **State:** Functional and byte-identity checks pass; cost adoption remains open; the longer-turn recheck retains an unresolved outlier.

The consumer is Detent:e060557, pinned to persistent-map:938d65e. Main retains the private tree. The generic source has conditional architectural approval and all seven enabled hosted library jobs pass. The consumer builds all enabled products and benchmarks locally and passes 234 native cases with one privileged-path skip among 235 registered cases. All six required clean public-pin hosted consumer jobs pass in run 40814412665; the first local build uses an explicit source override. A complete matching C++ rebuild is required because the underlying template namespace changes.

## Matched measurements

`tree-cost.json` contains 144 measured fresh processes: three rotating A/A/B triplets across 1K/100K keys, 34/200-byte keys, 1/128 updates per batch and 1/16 retained versions. Both controls execute the identical private-tree binary. Every process has a matching warmup. Logical sums, retained/allocated/collected nodes and collection-turn counts must match. Loaded and retained process-heap bytes match exactly across every arm. Thread CPU measures individual tree phases. Median case changes are small for loading and lookup, but several short update phases vary substantially. For 100K 34-byte keys, batch one and 16 retained roots, the 64-update phase has control medians 405/429 microseconds versus 522 microseconds after extraction. This is not declared a win or dismissed as noise; the longer-turn recheck is recorded separately below.

`production-cost.json` contains 24 measured fresh processes, three alternating pairs per workload, plus eight matching warmups. It uses the unchanged authority benchmark with 1K/100K knobs and 2/16 switch overrides. The benchmark verifies seeded state and update admission. CPU uses `std::clock()` (process CPU), while encoding is elapsed wall time. The controller’s wording “caller CPU” does not change that underlying clock. Median changes after extraction:

| Knobs | Overrides | Load CPU | Update CPU | Encode elapsed | Peak RSS |
| --- | --- | --- | --- | --- | --- |
| 1,000 | 2 | +2.08% | -0.91% | +14.05% | +20.51% |
| 1,000 | 16 | -0.14% | -8.64% | -8.71% | -15.97% |
| 100,000 | 2 | -0.39% | -0.26% | -0.02% | +1.39% |
| 100,000 | 16 | +1.01% | -3.49% | +1.87% | +0.57% |

The authority benchmark includes its legacy full-snapshot-copy comparison and retains both representations. Its RSS is a process envelope, not an isolated authority or agent footprint. Small-process RSS varies in both directions; the 100K cases increase 0.6–1.4%. The benchmark’s mapped `__TEXT` and `__DATA` segments match at 311,296 and 4,096 bytes respectively. Exact process heap equality in the tree screen is distinct from RSS. No speed or memory reduction is claimed.

## Compatibility and limits

The checkpoint and two snapshot exports are byte-identical; `wire-identity.json` and `fixtures/` preserve their hashes and bytes. Capture drivers reuse the actual production benchmark and existing export oracle, including chunk framing and retained-admission checks. Their printed timings run under a light hold and are not performance evidence.

The tree screen measures bounded collection with a live arena owner, then drained empty-arena replacement. It does not measure or guarantee bounded destruction of the final nonempty root. Per-collection wall maxima include clock overhead. Payload destruction costs remain caller-dependent. The source review covers allocator/control failures and retained-cursor lifetimes; the generic queue remains owner-driven.

These bounded workloads do not qualify durable ACK throughput, fleet fanout, 5M/10M capacity or SDK first-read latency. The resource, HTTP and bounded-codec cost gates remain separate. The initial tree controller’s “private-codec” label denotes the private AVL control, with no codec migration in either arm.

Sources, raw records, executable hashes and build metadata are retained here. Original scratch paths in scripts are provenance; reproducing the run requires matching source checkouts and regenerating CMake metadata. Both builds use Clang 23, C++20 and Release `-O3 -DNDEBUG`; no source policy change beyond the map extraction enters the paired authority target.

## Longer-turn recheck

`tree-cost-long.json` preserves 36 further fresh processes, with three rotating A/A/B triplets per 1K/100K and 34/200-byte key case. Batch size is one and 16 versions are retained; update turns increase from 64 to 1,024. Logical results, node counts and heap bytes still match. Three case update medians decrease, but the 100K/200-byte case increases 9.22% relative to the mean of the two control medians. Controls vary substantially too. This does not establish a causal source regression or clear the unexplained-cost gate. No production optimization is applied; the extraction stays isolated pending the bounded component recheck.

The saved Makefile flag copies normalize trailing whitespace; values and compiler options are unchanged.

## Within-process attribution

Two further records preserve 108 phases each, using both implementations and two identical private controls in one process. All six execution orders run twice, with three initial allocation placements. The first record (`within-process.json`) carries equivalent evolving histories across rounds; it does not recreate the same starting state each round and cannot be presented as the exact requested reset protocol. The corrected record (`within-process-reset.json`) rebuilds all three maps outside timing before every round, replays the same 1,024-key update trace and validates every value plus node counts. No sample is discarded.

The corrected record's median paired CPU changes against the mean same-round controls are -5.65%, +13.64% and +2.15% across the three placements. Identical-control phase differences range from -38.62% to +70.35%. The extracted arm exceeds both same-round controls in 11 of 36 rounds. This is inconclusive, not an established implementation regression or cost clearance. The architect's rule retains NO-GO for an inconclusive result. No source optimization or additional broad workload sweep follows from these data.
