# Capacity qualification

**Date:** 2026-10-22. **Status:** Measurement and runtime-capacity design in progress; no new supported ceiling declared.

Detent needs a measured operating envelope, including ingestion, replicated durability, distribution, durable local publication and application reads. Container microbenchmarks identify costs but cannot qualify that envelope. The [scale targets](SCALE_TARGETS.md) remain requirements, not completed qualification.

## Dataset sizes and reference

Run 1K, 10K, 100K, 130K, 500K, 1M and **1.3M** knobs. The primary target is 1.3M. Continue to 5M and 10M as exploratory tiers when resource admission permits. Use runtime configuration and the same executable at every size. Preserve rejected configurations and resource-limited trials in the results.

The supplied enterprise reference is approximately 130K knobs, 13 MiB wire size, 56 MiB peak decode RSS, 90 MB decoded agent memory and 1.5 seconds fetch time. It describes a reportedly inefficient implementation. It supplies a corpus-density reference, not an efficiency budget or a performance target. Hardware, corpus composition, compression, network, retained generations and the meaning of MB versus MiB are unknown. Do not compare an in-process parse measurement with its fetch time.

At the same density, 1.3M knobs would imply 130 MiB wire, 560 MiB peak decode RSS and 900 MB decoded agent memory. These are arithmetic reference projections, not Detent measurements or accepted budgets. Fetch time need not scale linearly.

Use both a reference-density mixed bool/int corpus and the existing short/maximum-length-name corpus with nested overrides. Record the seed, distributions, namespace scope and actual encoded size. Do not silently change definitions to make a larger tier pass. No real enterprise corpus has been supplied.

## Limits discovered before qualification

| Boundary | Current behavior | Required work |
| --- | --- | --- |
| Classic JSON parsing | Defaults to 100K knobs and 16 MiB; C++ parse calls accept explicit bounds | Carry count and byte settings through deployed configuration |
| Classic distributor | Has a count option; parser calls still use the default byte bound | Make count, body and decoded-byte bounds coherent |
| Legacy agent image | Compiled 4 MiB payload and 32-bit offsets | Keep refusals explicit; use the managed paged path for large-system qualification |
| Authority state | Defaults to 500K keys; constructor ceiling is 1M keys and 1 GiB image | Design coherent runtime capacity beyond 1M before claiming 1.3M support |
| Authority profile | Limits participate in peer identity, batch encoding and checkpoint validation | Preserve equality checks; startup configuration alone does not permit online raises |
| Managed delivery and local images | Default 500K record bounds in downstream components | Carry coherent capacity through decoder, builder, recovery and reader |
| Fleet harness | Scenario validation stops at 500K records | Extend admission and scenario bounds together |

A larger configuration must still bound input and allocation. Removing guards does not create capacity. Changing a recovered authority's semantic profile must continue to fail until a replicated limit-transition mechanism is implemented and qualified. Do not use an export into a fresh group as evidence of online growth.

## Measurement matrix

1. **Ingestion and retained state.** Measure construction, decode, steady retained RSS and requested heap, allocation count, encoded bytes and checkpoint disk bytes. Separate driver memory from service memory and include simultaneously retained generations.
2. **Durable updates.** Seed the dataset, then measure one-key, bounded-batch and concurrent producer updates with durable acknowledgments. Report offered and accepted rates, retries, queue pressure, CPU, disk bytes and p50/p95/p99/max latency. Exercise uniform keys and a hot namespace.
3. **Distribution and agents.** Measure cold bootstrap, warm reconnect, a small incremental update, durable local publication and complete oracle verification. Record authority, relay and agent RSS separately, plus aggregate peak, network bytes and fetch-to-ready time.
4. **Reads and reclamation.** Measure steady mapped SDK reads and startup/open cost, broad and hot working sets, group rollouts and misses. Measure partial cleanup turns and final-generation reclamation separately.
5. **Recovery and pressure.** Measure restart/checkpoint recovery, slow subscribers and retained generations. Record which admission limit refuses first and whether service remains responsive. A clean refusal is not a successful larger-tier run.

Use strict host reservations and existing interference checks. Advance one tier at a time; preflight memory, free disk and descriptors before starting. This host has 32 GiB physical RAM and shares resources with other work. Abort or refuse a trial before exhausting headroom, and retain the reason. A host-limited tier does not establish a product limit.

Publish bytes per knob and amplification relative to encoded size alongside absolute totals. Express update CPU as core-seconds per accepted operation; project fleet core demand only from measured rates. Do not infer dollars or power without a deployment price or power model. Do not call the largest completed tier the realistic limit until sustained writes, agent convergence and recovery meet explicit operating budgets.

## Validated architectural blueprint

The review covers scalability, edge cases, dependency/state ownership and security. Implement startup runtime capacity first: one explicit envelope forwards into existing limit types, preserves defaults and validates checked size arithmetic and overlapping resource funding before service admission. Use paged-v2. The 1.1M default page limit and hard uses of default segment/byte bounds must be included; exposing only the authority key setting is insufficient.

Qualify a fresh group configured for the complete 1.3M envelope, growing its contents through the matrix without recompiling or recreating it. This closes startup capacity only. Keep the application semantic profile in peer admission, batch framing and checkpoint validation unchanged during this stage.

Online profile growth is a subsequent versioned application component. An authorized committed transition must carry the expected old profile and replacement, persist the active profile and replay historical records under their original semantics. Update queued/prepared proposal handling and Node's cached decoder settings coherently; changing only State is invalid. Activate only after required participants support the new version. Initially support increases only. No export migration substitutes for this mechanism.

Container selection remains independent: the managed authority uses its persistent map and SDK reads use immutable pages. Hashing the classic Snapshot table cannot be credited with managed-path improvements. If the complete hash workload wins, qualify one canonical keyed-hash library component and its actual CPU costs before production selection; do not add a local duplicate primitive or require OpenSSL in the core client.

## Isolated model ladder, October 22

The [accepted model report](../benchmarks/model-capacity-20261022.json) records five alternating pairs at each completed point, with the same runtime-sized executable. The corpus is entirely boolean definitions with three overrides and short names. "Ordered" means ascending numeric IDs, not lexicographically sorted variable-width names. Values below are retained requested C++ heap, rounded to decimal MB; btree's range reflects insertion order.

| Knobs | Ownership-qualified btree | Standard maps | Hash snapshot index, standard nested maps |
| --- | --- | --- | --- |
| 130K | 27.3–29.2 MB | 38.5 MB | 38.0 MB |
| 1M | 210.2–224.2 MB | 296.0 MB | 293.2 MB |
| 1.3M | 273.3–291.6 MB | 384.8 MB | 377.2 MB |
| 5M | 1,051.3–1,121.4 MB | 1,480.0 MB | 1,452.7 MB |
| 10M | 2,102.7–2,242.2 MB | 2,960.0 MB | 2,905.4 MB |

At 10M, matched random-ID construction/validation/destruction CPU is 37.061 seconds for standard maps and 18.683 seconds for the hash index; ascending-ID CPU is 14.582 versus 21.449 seconds. Neither structure wins every insertion schedule.

The exploratory process peaks include both schedules in one process and allocation-tracking headers. At 10M, median peak RSS is 2.967 GB for btree, 5.676 GB for standard maps and 6.522 GB for the hash prototype. The hash's lower requested-byte count therefore does not establish lower process memory. These are instrumented model lifecycles, not production allocator or decoded-agent RSS. All accepted exploratory samples report zero swaps and zero major page faults; tracked live allocation balance returns to zero.

Background-process interference rejects several smaller-tier batches. Those points remain explicit gaps requiring retries, not interpolated measurements. No container trial closes the full-system capacity gate.
