# Five-million-knob final-client baseline

**Date:** 2026-10-25. **Source:** `4ad7dee`. **Scope:** Instrumented local read baseline; fast attachment is not implemented.

The saved agent generation at revision 39064 passes complete independent SDK verification with no daemon running. The measured process then performs 48 million checked reads over a seeded one-million-key reservoir. Each working set receives an untimed pass, and Client/pinned-view measurement order alternates across four repetitions. The [raw report](../../benchmarks/client-read-5m-20261025.json) preserves all cases, compiler/link commands, source and binary hashes, resource counters and evidence digest.

| Operation through Client | Working set | Median of per-repeat p50 | Median of per-repeat p99 |
| --- | --- | ---: | ---: |
| Integer | One hot key | 301 ns | 384 ns |
| Integer | Scattered sampled keys | 1.819 µs | 2.807 µs |
| Boolean default | One hot key | 518 ns | 643 ns |
| Boolean default | Scattered sampled keys | 1.471 µs | 3.072 µs |
| Cohort evaluation | One hot key | 811 ns | 1.082 µs |
| Cohort evaluation | Scattered sampled keys | 2.188 µs | 3.348 µs |

These are post-warmup instrumented latencies, including timing and result-comparison overhead. They are not cold first-touch measurements. Cohort evaluation includes definitions that return their default as well as percentage rollouts. Per-case CPU and fault counters cover all process threads. No timed case records a major fault. Four repetitions in one process are not four independent process starts, and small Client/view differences do not establish an optimization win.

Opening and validating the generation takes 59.690 seconds and 51.055 seconds of process CPU. The OS reports 21,141 minor faults and 353,609 major faults during that interval; cache residency is not forced. Exhaustive verification takes a further 33.515 seconds, including query-reservoir construction. Peak RSS is 1,680,850,944 bytes and includes benchmark query/timing storage, so it is not bare SDK memory or agent residency. The complete process finishes successfully in 176.760 seconds.

The initial diagnostic run lacks explicit warmup and fixed reader-order controls; its operation named `rollout` is mixed cohort evaluation. It remains exploratory evidence rather than the comparison above. The reviewed run adds those controls and explicit process-resource labels.

## Reproduction

Build `detent_system_mixed_oracle` and invoke its ordinary saved-generation verification command with `--benchmark 1000000 1000000` appended after the optional capacity arguments. The first integer is timed calls per case; the second is reservoir capacity. Benchmark mode preserves the complete semantic oracle. Use the same persisted generation and expected recipe file, the 5M capacity preset with 512 MiB string allowance, and an exclusive host hold. The report identifies their source trial and exact binary.

The [fast-client blueprint](../FAST_CLIENT_ARCHITECTURE.md) replaces per-client dataset-wide validation with authenticated rooted files and checked lazy paths. Acceptance measures Client construction through the first verified live value under 1 ms on resident data. This baseline fails that requirement; fast steady reads do not close the attachment gate.
