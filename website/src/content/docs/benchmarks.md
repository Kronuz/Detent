---
title: "Benchmarks"
---

Every number on this page comes from a benchmark in the repository's
`benchmarks/` directory or a recorded measurement artifact next to it. Each
one states what it measures, on what machine, and what it does **not** show.
Treat them as reproducible baselines, not service-level promises.

---

## Read Path

The read path is the part that matters most: it runs in your request handlers.

| Benchmark | What it measures | Command | Result |
| --- | --- | --- | --- |
| `detent_read_bench` | Steady-state `Client` read throughput, 4 threads | `detent_read_bench 4 2000000` | 6.71M ops/s (149.1 ns/op) |
| `detent_agent_shm_bench` | Shared-memory agent read throughput, 4 threads | `detent_agent_shm_bench 4 2000000` | 36.2M ops/s (27.6 ns/op) |
| `detent_hash_bench` | `stable_bucket()` alone (SHA-256 rollout hash) | `detent_hash_bench 5000000` | 5.05M calls/s (198.2 ns/call) |

Measured on one 14-core arm64 development machine, Release build.

The shared-memory read is about 5.4× faster in this benchmark; it evaluates
directly against a pre-parsed binary layout with no shared-pointer reference
counting. Rollout reads pay for one SHA-256 of the knob name plus
hash value; plain bool and int reads don't hash at all.

:::hint[Reading the per-op numbers]{.info}
At ~150 ns per read, a service doing 100,000 knob reads per second spends about
0.015 of one core on them. That's an arithmetic projection from the benchmark,
not a measured service cost.
:::

---

## Update Path

| Benchmark | What it measures | Command | Result |
| --- | --- | --- | --- |
| `detent_update_bench` | Parse + validate + merge for a 100-knob snapshot | `detent_update_bench 200000 100` | 28.6K updates/s (35.0 µs/update) |

Updates run on the background thread, never on the read path.

### At Scale

The scale benchmark uses a synthetic corpus of 100 namespaces, half bool and
half int, with two group overrides per bool. Release build on macOS x86_64,
Apple Clang 17. Medians of five repetitions at 100,000 knobs and three at
500,000.

| Knobs | JSON bytes | JSON parse | Apply one-knob PATCH | Flat image encode |
| ---: | ---: | ---: | ---: | ---: |
| 100,000 | 11,123,377 | 296.130 ms | 148.624 ms | 19.778 ms |
| 500,000 | 56,283,377 | 1,708.244 ms | 780.557 ms | 108.445 ms |

The legacy distributor copies the whole map to apply a `PATCH`, which is why a
one-knob change costs as much as it does at this size. The durable authority
was built to remove exactly that cost:

| Knobs | Legacy patch, median | Authority update, median | Tree nodes allocated per update |
| ---: | ---: | ---: | ---: |
| 100,000 | 133.708 ms | 0.0127 ms | 26 |
| 500,000 | 668.599 ms | 0.0113 ms | 27 |

Those are application-only measurements: no I/O, HTTP or replication. With
durable POSIX storage and 64 producers grouped per commit, the authority
measured about 490 transactions per second on a local disk, with a 500,000-knob
state consuming about 0.042 CPU cores at that rate. See
[Authority](/Detent/architecture/authority/) for the full tables.

```sh
build/benchmarks/detent_scale_bench 100000 5
build/benchmarks/detent_authority_bench 500000 uniform 2
build/benchmarks/detent_authority_durable_bench .scratch/authority-500k 500000 64
```

---

## Distribution Fan-out

`detentd_fanout_bench` runs one distributor and N simulated pollers on the same
machine with a 200 ms poll interval, publishes an update, and measures how long
it takes every poller to observe it.

| Watchers | p50 fan-out latency | p99 fan-out latency |
| ---: | --- | --- |
| 100 | ~156–165 ms | ~166–170 ms |
| 1,000 | ~30–100 ms | ~200–215 ms |
| 2,000 | ~105–145 ms | ~200 ms to 2.4 s |

Through 1,000 watchers, propagation tracks the poll interval. At 2,000 the
single-machine harness degrades unpredictably and can't tell whether the
distributor or the load generator is the bottleneck.

```sh
build/benchmarks/detentd_fanout_bench 1000 3000 200 4
```

### Bytes on the Wire

For one small update, measured against a real distributor:

| Response | Bytes |
| --- | ---: |
| Full snapshot | 276 |
| Cached diff for the same update | 138 |
| Full snapshot, gzip | 173 |

Diffs and compressed bodies are computed once per accepted update and cached,
so serving them costs no per-request work.

---

## Footprint

Release builds on macOS x86_64, stripped. Idle RSS is the median of five
samples after one second of startup, with an empty distributor and an agent
with one small defaults file.

| Executable | Stripped size | Idle RSS | Threads |
| --- | ---: | ---: | ---: |
| `detentd` | 939,776 bytes | 2,068 KiB | 38 |
| `detent-agent` | 475,520 bytes | 1,316 KiB | 2 |
| `detent` | 257,864 bytes | not sampled | 1 |

These are idle resident-memory samples, not peak allocation or throughput
measurements.

---

## What Hasn't Been Measured

:::hint[Open measurements]{.construction}
1. Real network conditions: every fan-out number above is loopback.
2. Multi-hour or multi-day soak runs.
3. Fleet-scale distribution: the planning target of 50,000 agents is a test
   target, not demonstrated capacity.
4. Power or dollar cost: none of these benchmarks measured either, so no
   savings are claimed.
:::

The raw artifacts live in
[`benchmarks/*.json`](https://github.com/Kronuz/Detent/tree/main/benchmarks),
each with its corpus, hardware and build details.
