# Shared resource adoption qualification

**Status:** Functional/platform gates pass; adoption remains blocked by first-read performance attribution. The [independent ruling](resource-adoption-component-2026-10-27.md) preserves the original gate. These measurements do not establish a general speedup or equivalent footprint.

The isolated migration replaces the allocation, arena, buffer and retirement bodies with canonical allocator types and forwards construction. DiskLedger keeps its policy and uses the shared reservation book. The production change, Detent:39f19a4, adds 62 lines and removes 1,046, including the private retirement translation unit. Prepared metadata retains its exact four-entry footprint formula. Consensus and unrelated dependency pins are unchanged.

## Correctness and closure

1. The full native profile registers 235 tests: 234 pass, one privileged fixture skips, no failures, 817.57 seconds. Non-skipped privileged coverage passes through hosted and FreeBSD execution.
2. Candidate hosted runs [40482489111](https://github.com/Kronuz/Detent/actions/runs/40482489111) and [40486408544](https://github.com/Kronuz/Detent/actions/runs/40486408544) each pass all six enabled Linux/macOS jobs, including ASan/UBSan. FreeBSD and hosted TSan are disabled and qualified separately.
3. Local ASan/UBSan passes 17 distinct cases across two invocations. The first prepared-startup invocation lacks its CLI executable; Detent:be43e02 adds the missing build dependency, then all four startup/readiness variants pass. The other 13 cases pass in the first invocation. No sanitizer error is suppressed. TSan passes storage, retirement and retirement-factory checks, 3/3 in 1.30 seconds.
4. Fresh-public-source FreeBSD native passes 170/170 in 238.17 seconds. Its first sanitizer invocation passes 14 cases but omits the privileged fixture executable; after building it, that case passes without a skip in 0.15 seconds. This is 15 distinct sanitizer cases. Unsupported FreeBSD LeakSanitizer is explicitly disabled.
5. The [closure audit](../measurements/resource-closure-20261027.json) verifies populated public revisions without source overrides across full, reader-only, ASan and TSan builds. The reader-only link contains one canonical retirement library and no allocation-interception definitions. All four daemon/backend flags are disabled in that reader profile.

## SDK first reads

The earlier [baseline profile](../measurements/resource-baseline-reader-20261027.json) meets 200/200 below 1 ms, while the later [migration profile](../measurements/resource-migrated-reader-20261027.json) meets 194/200. Because these profiles run at different times, the [counterbalanced comparison](../measurements/resource-sdk-pairs-20261027.json) alternates both binaries per key and round under one exclusive hold, with a separate warmup process for each measured process.

| Metric | Before | Shared resources |
| --- | ---: | ---: |
| First-read median | 0.752 ms | 0.769 ms |
| First-read p99 | 1.094 ms | 1.056 ms |
| First-read maximum | 1.158 ms | 1.552 ms |
| Below 1 ms | 196/200 | 195/200 |
| Major faults | 0 | 0 |
| Median process maximum RSS | 1,130,496 B | 1,146,880 B |

The paired median first-read change is +21.908 microseconds. Caller attachment and lookup contribute paired medians of +9.532 and +9.415 microseconds; those medians are not additive. Paired process CPU adds 24 microseconds and minor faults add four. Process CPU includes cleanup work and does not isolate the calling thread. Eight of nine samples above 1 ms also consume at least 1 ms process CPU, so descheduling alone is not an established explanation. The paired steady-read mean changes by a median of -44.41 ns; this does not cancel first-read cost. No timing excludes required production initialization.

## Ownership and storage costs

[Five ownership/retirement pairs](../measurements/resource-ownership-pairs-20261027.json) use identical observation source. Shared-account construction occupies 128 bytes in two allocations versus 80 bytes in one; both release every observed allocation. The account facade shrinks from 48 to 16 bytes, charges grow from 16 to 24, page and allocation headers remain 48/32, and the arena grows from 72 to 80.

Median process CPU adds 61.39 ns per account construction, 8.679 ns per root admission and 22.65 ns per arena. Root admission's delta normalizes to 0.00868 core at one million admissions per second; that is not measured production traffic or a power estimate. Retirement of 100,000 owners changes from 0.222103 to 0.225040 seconds CPU. Admitted peak remains 8,800,336 bytes, fixed runtime heap 432 bytes and configured stack reservation 1 MiB. Payload and control charges are zero after runtime/domain release.

[Three paged recovery/collection pairs](../measurements/resource-paged-pairs-20261027.json) at 100,000 keys preserve mapping, validation and operation counts. Recovery heap peaks add 208–432 bytes and its median RSS adds 256 KiB. Collection's admitted peak adds 384 bytes; process RSS medians change from 72,122,368 to 74,657,792 bytes, with overlapping observed ranges. Small warm timings vary in both directions. These samples quantify observed cost and conservation, not statistical equivalence.

The [controllers](../../benchmarks/resource-qualification/) use the restored CMake benchmark targets and the ordinary prepared-client probe. Run them through `quiet exclusive` with matched compiler flags and the recorded prepared corpus. The baseline executable snapshot is Detent:f3e6c68; migration production is Detent:39f19a4 with observation code Detent:6427b5e. The baseline snapshot is reachable through the candidate merge. Raw records retain binary hashes, all samples and method limits.

## Bounded caller diagnostic

The [single fixed diagnostic batch](../measurements/resource-caller-diagnostic-20261027.json) completes 320 measured processes: 40 keys, two counterbalanced rounds, two arms and separate unhooked/allocation-observer modes. Each measured process has its own matching warmup. The [common observer and archived controllers](../measurements/resource-caller-diagnostic/) retain required production initialization inside the measured phases. All values and the 5M cardinality check pass; no major faults occur in either unhooked arm.

| Phase | Before caller CPU median | Shared caller CPU median | Paired caller CPU difference | Paired minor-fault difference | Paired allocation-event difference |
| --- | ---: | ---: | ---: | ---: | ---: |
| Source factory | 10.111 µs | 11.414 µs | +1.438 µs | +1 | 0 |
| Client construction | 437.534 µs | 431.328 µs | −6.187 µs | −1 | +4 |
| First lookup | 274.924 µs | 288.094 µs | +8.966 µs | +4 | 0 |

CPU and fault columns use only the 80 unhooked samples per arm. Allocation differences use the separate 80 observed samples per arm; every Client pair adds exactly four events, while factory and lookup counts are identical. Faults and switches are process-wide; caller CPU excludes the retirement worker. Per-phase and paired medians are not additive.

The lookup difference is visible in caller CPU, not merely elapsed scheduling delay. It accompanies additional minor faults without additional allocation events. These observations locate work but do not identify a particular faulting mapping, instruction or justified source fix. Client construction does not show a median CPU regression in this batch despite its additional allocations. The sum of instrumented phase wall times remains below 1 ms for all 80 samples in each arm; this is diagnostic evidence, not acceptance, and does not erase the earlier ordinary-probe misses.

The performance gate remains open. No unchanged acceptance run is repeated until green, and no criterion is weakened without an explicit performance decision.
